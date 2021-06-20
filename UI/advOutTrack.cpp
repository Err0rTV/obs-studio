#include <QGuiApplication>
#include <QWidget>

#include <obs.hpp>
#include <util/util.hpp>
#include "obs-app.hpp"

#include "window-basic-main.hpp"
#include "window-basic-settings.hpp"
#include "audio-encoders.hpp"
#include "advOutTrack.hpp"

advOutTrack::advOutTrack(OBSBasicSettings *parent, QWidget *parentWidget,
			 int n_)
	: QWidget(parentWidget), ui(new Ui::advOutTrack), n(n_)

{
	ui->setupUi(this);

	OBSBasic *main = reinterpret_cast<OBSBasic *>(App()->GetMainWindow());
	auto name = QString("Track%1Name").arg(n + 1);
	auto btrate = QString("Track%1Bitrate").arg(n + 1);

	int trackBitrate =
		config_get_uint(main->Config(), "AdvOut", btrate.toLocal8Bit());
	const char *name1 =
		config_get_string(main->Config(), "AdvOut", name.toLocal8Bit());

	trackBitrate = FindClosestAvailableAACBitrate(trackBitrate);

	auto t = QString("%1").arg(trackBitrate);
	this->name = name1;
	this->bitrate = trackBitrate;
	ui->advOutTrackName->setText(name1);
	ui->advOutTrackBitrate->setCurrentIndex(
		ui->advOutTrackBitrate->findText(t));
	auto ti = QString(QCoreApplication::translate(
		"main", "Basic.Settings.Output.Adv.Audio.Track"));
	ti.append(QString(" %1").arg(n + 1));
	ui->groupBox->setTitle(ti);
	ui->advOutTrackBitrate->installEventFilter(this);

	QObject::connect(ui->advOutTrackBitrate, COMBO_CHANGED, parent,
			 OUTPUTS_CHANGED);
	QObject::connect(ui->advOutTrackName, EDIT_CHANGED, parent,
			 OUTPUTS_CHANGED);

	connect(ui->advOutTrackBitrate, SIGNAL(currentIndexChanged(int)),
		parent, SLOT(UpdateStreamDelayEstimate()));
	connect(ui->advOutTrackBitrate, SIGNAL(currentIndexChanged(int)), this,
		SLOT(AdvReplayBufferChanged()));

	this->setProperty("changed", QVariant(false));
}

advOutTrack::~advOutTrack() {}

void advOutTrack::setName(QString name) {}
void advOutTrack::setBitrate(int n) {}

void advOutTrack::_populateAACBitrates()
{
	PopulateAACBitrates({ui->advOutTrackBitrate});
	saveConfig(getBitrateName().toLocal8Bit());
}

void advOutTrack::_restrictResetBitrates(int bitrate)
{
	RestrictResetBitrates({ui->advOutTrackBitrate}, bitrate);
	saveConfig(getBitrateName().toLocal8Bit());
}

void advOutTrack::saveConfig(const char *value)
{
	OBSBasic *main = reinterpret_cast<OBSBasic *>(App()->GetMainWindow());
	config_set_string(
		main->Config(), "AdvOut", value,
		this->ui->advOutTrackBitrate->currentText()
			.toUtf8()); //TODO: duplicate in windows-basic-settings.cpp
}
