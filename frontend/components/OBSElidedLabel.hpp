
#pragma once

#include <QLabel>

/* A label that automatically truncates its text
 * if it doesn't have enough space to show it fully.
 */
class OBSElidedLabel : public QLabel {
	Q_OBJECT;

public:
	OBSElidedLabel(QWidget *parent = (QWidget *)nullptr, Qt::WindowFlags f = Qt::WindowFlags());
	OBSElidedLabel(const QString &text, QWidget *parent = (QWidget *)nullptr,
		       Qt::WindowFlags f = Qt::WindowFlags());

	void setToolTip(const QString &text);

public slots:
	void setText(const QString &text);

protected:
	virtual void resizeEvent(QResizeEvent *event) override;
	virtual void paintEvent(QPaintEvent *event) override;

private:
	QString elidedText = "";
	QRect elidedTextBounds{};
	bool hasToolTip = false;

	void updateElidedText();
};
