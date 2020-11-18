#pragma once

#include <obs.hpp>
#include <QWidget>
#include <QPointer>
#include <QDoubleSpinBox>
#include <QStackedWidget>
#include "balance-slider.hpp"
#include "obs-app.hpp"
#include "window-basic-main.hpp"

class QGridLayout;
class QLabel;
class QSpinBox;
class QCheckBox;
class QComboBox;

enum class VolumeType {
	dB,
	Percent,
};

/* ------------------------------------------------------------------------- */
/* Qt event queue source callbacks */
static inline void setCheckboxState(QCheckBox *checkbox, bool checked)
{
	checkbox->blockSignals(true);
	checkbox->setChecked(checked);
	checkbox->blockSignals(false);
}

class OBSmixert : public QCheckBox {
	Q_OBJECT
public:
	//	operator QCheckBox*() const { return NULL;  }
	OBSmixert(){};
	OBSmixert(int n_, OBSSource source_) : source(source_), n(n_)
	{
		uint32_t mixers = obs_source_get_audio_mixers(source);
		OBSBasic *main =
			reinterpret_cast<OBSBasic *>(App()->GetMainWindow());

		QString TrackName = QString("Track%1Name").arg(n + 1);
		const char *name1 = config_get_string(main->Config(), "AdvOut",
						      TrackName.toLocal8Bit());
		this->setText(name1);

		this->setChecked(mixers & (1 << n));

		QWidget::connect(this, SIGNAL(clicked(bool)), this,
				 SLOT(mixerChanged(bool)));
	};
	void setState(uint32_t mixers)
	{
		//uint32_t mixers = obs_source_get_audio_mixers(source);
		setCheckboxState(this, mixers & (1 << this->n));
	}

private:
	int n;
	OBSSource source;

	static inline void setMixer(obs_source_t *source, const int mixerIdx,
				    const bool checked)
	{
		uint32_t mixers = obs_source_get_audio_mixers(source);
		uint32_t new_mixers = mixers;

		if (checked)
			new_mixers |= (1 << mixerIdx);
		else
			new_mixers &= ~(1 << mixerIdx);

		obs_source_set_audio_mixers(source, new_mixers);
	}
public slots:
	void mixerChanged(bool checked)
	{
		this->setMixer(source, this->n, checked);
	}
};

class OBSAdvAudioCtrl : public QObject {
	Q_OBJECT

private:
	OBSSource source;

	QPointer<QWidget> activeContainer;
	QPointer<QWidget> forceMonoContainer;
	QPointer<QWidget> mixerContainer;
	QPointer<QWidget> balanceContainer;

	QPointer<QLabel> iconLabel;
	QPointer<QLabel> nameLabel;
	QPointer<QLabel> active;
	QPointer<QStackedWidget> stackedWidget;
	QPointer<QSpinBox> percent;
	QPointer<QDoubleSpinBox> volume;
	QPointer<QCheckBox> forceMono;
	QPointer<BalanceSlider> balance;
	QPointer<QLabel> labelL;
	QPointer<QLabel> labelR;
	QPointer<QSpinBox> syncOffset;
	QPointer<QComboBox> monitoringType;

	QVector<OBSmixert *> vmixers;

	OBSSignal volChangedSignal;
	OBSSignal syncOffsetSignal;
	OBSSignal flagsSignal;
	OBSSignal mixersSignal;
	OBSSignal activateSignal;
	OBSSignal deactivateSignal;

	static void OBSSourceActivated(void *param, calldata_t *calldata);
	static void OBSSourceDeactivated(void *param, calldata_t *calldata);
	static void OBSSourceFlagsChanged(void *param, calldata_t *calldata);
	static void OBSSourceVolumeChanged(void *param, calldata_t *calldata);
	static void OBSSourceSyncChanged(void *param, calldata_t *calldata);
	static void OBSSourceMixersChanged(void *param, calldata_t *calldata);

public:
	OBSAdvAudioCtrl(QGridLayout *layout, obs_source_t *source_);
	virtual ~OBSAdvAudioCtrl();

	inline obs_source_t *GetSource() const { return source; }
	void ShowAudioControl(QGridLayout *layout);

	void SetVolumeWidget(VolumeType type);
	void SetIconVisible(bool visible);

public slots:
	void SourceActiveChanged(bool active);
	void SourceFlagsChanged(uint32_t flags);
	void SourceVolumeChanged(float volume);
	void SourceSyncChanged(int64_t offset);
	void SourceMixersChanged(uint32_t mixers);

	void volumeChanged(double db);
	void percentChanged(int percent);
	void downmixMonoChanged(bool checked);
	void balanceChanged(int val);
	void syncOffsetChanged(int milliseconds);
	void monitoringTypeChanged(int index);
	//	void mixerChanged(int i, bool checked);
	void ResetBalance();
};
