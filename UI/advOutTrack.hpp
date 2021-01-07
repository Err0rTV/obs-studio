#pragma once

#include <QWidget>
#include <QPointer>

#include <obs.hpp>
#include "ui_advOutTrack.h"

class OBSBasicSettings;

	/** 
 * @ingroup UI_OBS
 */
class advOutTrack : public QWidget {
	Q_OBJECT

private:
	std::unique_ptr<Ui::advOutTrack> ui;

protected:
	/**
	* @brief prevent value change by the mouse wheel
	*/
	bool eventFilter(QObject *o, QEvent *e)
	{
		if (e->type() == QEvent::Wheel ||
		    qobject_cast<QAbstractSpinBox *>(o)) {
			e->ignore();
			return true;
		}
		return QWidget::eventFilter(o, e);
	}

public:
	advOutTrack(OBSBasicSettings *, QWidget *parentWidget, int n);
	~advOutTrack();
	void setName(QString name);
	void setBitrate(int n);
	void _populateAACBitrates(void);
	void _restrictResetBitrates(int);
	void saveConfig(const char *value);

	QString getName() {  return QString("Track%1Name").arg(n+1); }
	QString getBitrateName() { return QString("Track%1Bitrate").arg(n+1); }
	QComboBox *getTrackBitrate() { return ui->advOutTrackBitrate;  }
	QLineEdit *getTrackName() { return ui->advOutTrackName;  }

	QString name;
	unsigned int bitrate;
	int n;

//public slots:
//	void OutputChanged();
};
