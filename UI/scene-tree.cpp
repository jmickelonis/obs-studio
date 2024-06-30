#include "scene-tree.hpp"

#include <QSizePolicy>
#include <QScrollBar>
#include <QDropEvent>
#include <QPushButton>
#include <QTimer>
#include <cmath>

SceneTree::SceneTree(QWidget *parent_) : QListWidget(parent_)
{
	installEventFilter(this);
	setDragDropMode(InternalMove);
	setMovement(QListView::Snap);
}

void SceneTree::SetGridMode(bool grid)
{
	parent()->setProperty("gridMode", grid);
	gridMode = grid;

	if (gridMode) {
		setResizeMode(QListView::Adjust);
		setViewMode(QListView::IconMode);
		setUniformItemSizes(true);
		setStyleSheet("*{padding: 0; margin: 0;}");
	} else {
		setViewMode(QListView::ListMode);
		setResizeMode(QListView::Fixed);
		setStyleSheet("");
	}

	QResizeEvent event(size(), size());
	resizeEvent(&event);
}

bool SceneTree::GetGridMode()
{
	return gridMode;
}

void SceneTree::SetGridItemWidth(int width)
{
	maxWidth = width;
}

void SceneTree::SetGridItemHeight(int height)
{
	itemHeight = height;
}

int SceneTree::GetGridItemWidth()
{
	return maxWidth;
}

int SceneTree::GetGridItemHeight()
{
	return itemHeight;
}

bool SceneTree::eventFilter(QObject *obj, QEvent *event)
{
	return QObject::eventFilter(obj, event);
}

void SceneTree::resizeEvent(QResizeEvent *event)
{
	int count = this->count();

	if (gridMode) {
		if (!count) {
			setGridSize(QSize(1, 1));
			setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		} else {
			QRect rect = contentsRect();
			int w = rect.width() -
				1; // need to subtract 1 for some reason
			columns = (int)std::ceil((float)w / maxWidth);
			rows = (int)std::ceil((float)count / columns);

			if ((rows * itemHeight) > rect.height()) {
				setVerticalScrollBarPolicy(
					Qt::ScrollBarAlwaysOn);
				w -= verticalScrollBar()->sizeHint().width();
				columns = (int)std::ceil((float)w / maxWidth);
				rows = (int)std::ceil((float)count / columns);
			} else {
				setVerticalScrollBarPolicy(
					Qt::ScrollBarAlwaysOff);
			}

			columns = std::min(columns, count);
			itemWidth = std::min(w / columns, maxWidth);
			setGridSize(QSize(itemWidth, itemHeight));

			for (int i = 0; i < count; i++)
				item(i)->setSizeHint(
					QSize(itemWidth, itemHeight));
		}
	} else {
		setGridSize(QSize());
		for (int i = 0; i < count; i++)
			item(i)->setData(Qt::SizeHintRole, QVariant());
	}

	QListWidget::resizeEvent(event);
}

void SceneTree::startDrag(Qt::DropActions supportedActions)
{
	QListWidget::startDrag(supportedActions);
}

void SceneTree::dropEvent(QDropEvent *event)
{
	if (event->source() != this) {
		QListWidget::dropEvent(event);
		return;
	}

	if (gridMode) {
		if (dropIndex < 0 || dropIndex >= count())
			return;

		// The position has to correspond to a grid location,
		// or Qt will not allow the move
		int row = dropIndex / columns;
		int column = dropIndex % columns;
		QRect rect = contentsRect();
		QPointF position(rect.x() + column * itemWidth,
				 rect.y() + row * itemHeight);
		event = new QDropEvent(position, event->possibleActions(),
				       event->mimeData(), event->buttons(),
				       event->modifiers());

		QListWidgetItem *item = takeItem(selectedIndexes()[0].row());
		insertItem(dropIndex, item);
		setCurrentItem(item);
	}

	QListWidget::dropEvent(event);

	if (gridMode) {
		// We must call resizeEvent to correctly place all grid items.
		// We also do this in rowsInserted.
		QResizeEvent resEvent(size(), size());
		SceneTree::resizeEvent(&resEvent);
	}

	QTimer::singleShot(100, [this]() { emit scenesReordered(); });
}

void SceneTree::RepositionGrid(QDragMoveEvent *event)
{
	int count = this->count();

	if (event) {
		QPoint point = event->position().toPoint();
		QRect rect = contentsRect();
		int column = std::max(0, (point.x() - rect.x()) / itemWidth);
		int row = std::max(0, (point.y() - rect.y()) / itemHeight);

		column = std::max(std::min(column, columns - 1), 0);
		row = std::max(std::min(row, rows - 1), 0);

		if (row >= 0 && row == rows - 1) {
			// If out of bounds in the last row,
			// move to the row above
			int remainder = count % columns;
			if (remainder && column >= remainder)
				row--;
		}

		int src = selectedIndexes()[0].row();
		int dst = (row * columns) + column;

		if (dst != src) {
			dropIndex = dst;

			// We have a drop spot
			// Shift the other items to show the result
			int offset = 0;
			int incIndex = dst > src ? dst + 1 : dst;
			for (int i = 0; i < count; i++) {
				auto *wItem = item(i);
				if (i == incIndex)
					offset += 1;
				else if (i == src)
					offset -= 1;
				int j = wItem->isSelected() ? dst : i + offset;
				QPoint position =
					QPoint((j % columns) * itemWidth,
					       (j / columns) * itemHeight);
				QModelIndex index = indexFromItem(wItem);
				setPositionForIndex(position, index);
			}

			return;
		}
	}

	dropIndex = -1;

	// Move items back to their original positions
	for (int i = 0; i < count; i++) {
		auto *wItem = item(i);
		QPoint position = QPoint((i % columns) * itemWidth,
					 (i / columns) * itemHeight);
		QModelIndex index = indexFromItem(wItem);
		setPositionForIndex(position, index);
	}
}

void SceneTree::dragMoveEvent(QDragMoveEvent *event)
{
	if (gridMode) {
		RepositionGrid(event);
	}

	QListWidget::dragMoveEvent(event);
}

void SceneTree::dragLeaveEvent(QDragLeaveEvent *event)
{
	if (gridMode) {
		RepositionGrid();
	}

	QListWidget::dragLeaveEvent(event);
}

void SceneTree::rowsInserted(const QModelIndex &parent, int start, int end)
{
	QResizeEvent event(size(), size());
	SceneTree::resizeEvent(&event);

	QListWidget::rowsInserted(parent, start, end);
}

#if QT_VERSION < QT_VERSION_CHECK(6, 4, 3)
// Workaround for QTBUG-105870. Remove once that is solved upstream.
void SceneTree::selectionChanged(const QItemSelection &selected,
				 const QItemSelection &deselected)
{
	if (selected.count() == 0 && deselected.count() > 0 &&
	    !property("clearing").toBool())
		setCurrentRow(deselected.indexes().front().row());
}
#endif
