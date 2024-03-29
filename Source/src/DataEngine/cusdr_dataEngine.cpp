/**
* @file  cusdr_dataEngine.cpp
* @brief cuSDR data engine class
* @author Hermann von Hasseln, DL3HVH
* @version 0.1
* @date 2011-02-02
*/

/*
 *   
 *   Copyright 2010 Hermann von Hasseln, DL3HVH
 *
 *	 using original C code by John Melton, G0ORX/N6LYT and Dave McQuate, WA8YWQ
 *   
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License version 2 as
 *   published by the Free Software Foundation
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

//#define TESTING

#define LOG_DATA_ENGINE
// use DATA_ENGINE_DEBUG
#define LOG_DATA_PROCESSOR
// use DATA_PROCESSOR_DEBUG
#define LOG_AUDIO_PROCESSOR
// use AUDIO_PROCESSOR
#define LOG_WIDEBAND_PROCESSOR
// use WIDEBAND_PROCESSOR_DEBUG

#include "cusdr_dataEngine.h"


/*!
	\class DataEngine
	\brief The DataEngine class implements the main SDR functionality.
*/
/*!
	\brief Implements interfaces to the HPSDR hardware and various Server and DSP functionality.
	- set up HW interfaces to Metis or other resp.
	- initializes Metis.
	- set up parameters for HPSDR hardware.
	- implements the data receiver thread.
	- implements the data processor thread.
	- implements the wide band data processor thread.
	- implements the audio receiver thread.
	- implements the audio processor thread.
	- implements the interface to the Chirp WSPR decoding functionality.
*/

static quint8  adc_rx1_4, adc_rx5_8, adc_rx9_16;
static quint8  new_adc_rx1_4, new_adc_rx5_8, new_adc_rx9_16;

DataEngine::DataEngine(QObject *parent)
	: QObject(parent)
	, set(Settings::instance())
	, m_serverMode(set->getCurrentServerMode())
	, m_hwInterface(set->getHWInterface())
	, m_dataEngineState(QSDR::DataEngineDown)
	, m_meterType(SIGNAL_STRENGTH)
	, m_restart(false)
	, m_networkDeviceRunning(false)
	, m_soundFileLoaded(false)
	, m_chirpInititalized(false)
	, m_discoveryThreadRunning(false)
	, m_dataIOThreadRunning(false)
	, m_chirpDataProcThreadRunning(false)
	, m_dataProcThreadRunning(false)
	, m_audioRcvrThreadRunning(false)
	, m_audioInProcThreadRunning(false)
	, m_audioOutProcThreadRunning(false)
	, m_frequencyChange(false)
	//, m_wbSpectrumAveraging(set->getSpectrumAveraging())
	//, m_wbSpectrumAveraging(true)
	, m_hamBandChanged(true)
	, m_chirpThreadStopped(true)
	, m_hpsdrDevices(0)
	, m_configure(10)
	, m_timeout(5000)
	, m_remainingTime(0)
	, m_RxFrequencyChange(0)
	, m_forwardPower(0)
	, m_rxSamples(0)
	, m_chirpSamples(0)
	, m_spectrumSize(set->getSpectrumSize())
	, m_sendState(0)
	, m_sMeterCalibrationOffset(0.0f)//(35.0f)
{
	qRegisterMetaType<QAbstractSocket::SocketError>();

	this->setObjectName(QString::fromUtf8("dataEngine"));

	m_clientConnected = false;

	//currentRx = 0;
	m_discoverer = 0;
	m_dataIO = 0;
	m_dataProcessor = 0;
	m_wbDataProcessor = 0;
	m_audioReceiver = 0;
	m_audioOutProcessor = 0;
	m_chirpProcessor = 0;
	//m_wbAverager = 0;

	set->setMercuryVersion(0);
	set->setPenelopeVersion(0);
	set->setPennyLaneVersion(0);
	set->setMetisVersion(0);
	set->setHermesVersion(0);

	io.metisFW = 0;
	io.hermesFW = 0;
	io.mercuryFW = 0;
	
    //m_audioBuffer.resize(0);
    //m_audiobuf.resize(IO_BUFFER_SIZE);

	initAudioEngine();
	setupConnections();

	// test
    /*audioringbuffer.reserve(3);
		
    audioringbuffer.append(1);
	audioringbuffer.append(2);
	audioringbuffer.append(3);

	for (int i = 0; i < audioringbuffer.size(); ++i) {
		qDebug() << audioringbuffer.at(i);
	}

	audioringbuffer.append(4);
	audioringbuffer.append(5);

	for (int i = 0; i < audioringbuffer.size(); ++i) {
		qDebug() << audioringbuffer.at(i);
    }*/

	m_message = "audio sample = %1";
	m_counter = 0;
}

DataEngine::~DataEngine() {
}

void DataEngine::initAudioEngine() {
	m_audioEngine = new AudioEngine();
	//m_audioEngine->setSystemState(QSDR::NoError, m_hwInterface, m_serverMode, m_dataEngineState);
}

void DataEngine::setupConnections() {

	CHECKED_CONNECT(
		set,
		SIGNAL(systemStateChanged(
					QObject *,
					QSDR::_Error,
					QSDR::_HWInterfaceMode,
					QSDR::_ServerMode,
					QSDR::_DataEngineState)),
		this,
		SLOT(systemStateChanged(
					QObject *,
					QSDR::_Error,
					QSDR::_HWInterfaceMode,
					QSDR::_ServerMode,
					QSDR::_DataEngineState)));

	CHECKED_CONNECT(
		set, 
		SIGNAL(rxListChanged(QList<Receiver *>)),
		this,
		SLOT(rxListChanged(QList<Receiver *>)));

	CHECKED_CONNECT(
		set, 
		SIGNAL(numberOfRXChanged(QObject *, int)), 
		this, 
		SLOT(setNumberOfRx(QObject *, int)));

	CHECKED_CONNECT(
		set, 
		SIGNAL(currentReceiverChanged(QObject *,int)),
		this, 
		SLOT(setCurrentReceiver(QObject *, int)));

	CHECKED_CONNECT(
		set,
		SIGNAL(hamBandChanged(QObject *, int, bool, HamBand)),
		this,
		SLOT(setHamBand(QObject *, int, bool, HamBand)));

	CHECKED_CONNECT(
		set,
		SIGNAL(sampleRateChanged(QObject *, int)), 
		this, 
		SLOT(setSampleRate(QObject *, int)));

	CHECKED_CONNECT(
		set, 
		SIGNAL(mercuryAttenuatorChanged(QObject *, HamBand, int)),
		this, 
		SLOT(setMercuryAttenuator(QObject *, HamBand, int)));

//	CHECKED_CONNECT(
//		set,
//		SIGNAL(mercuryAttenuatorsChanged(QObject *, QList<int>)),
//		this,
//		SLOT(setMercuryAttenuators(QObject *, QList<int>)));

	CHECKED_CONNECT(
		set,
		SIGNAL(ditherChanged(QObject *, int)), 
		this, 
		SLOT(setDither(QObject *, int)));

	CHECKED_CONNECT(
		set, 
		SIGNAL(randomChanged(QObject *, int)), 
		this, 
		SLOT(setRandom(QObject *, int)));

	CHECKED_CONNECT(
		set, 
		SIGNAL(src10MhzChanged(QObject *, int)), 
		this, 
		SLOT(set10MhzSource(QObject *, int)));

	CHECKED_CONNECT(
		set, 
		SIGNAL(src122_88MhzChanged(QObject *, int)), 
		this, 
		SLOT(set122_88MhzSource(QObject *, int)));

	CHECKED_CONNECT(
		set, 
		SIGNAL(micSourceChanged(QObject *, int)), 
		this, 
		SLOT(setMicSource(QObject *, int)));

	CHECKED_CONNECT(
		set, 
		SIGNAL(classChanged(QObject *, int)), 
		this, 
		SLOT(setMercuryClass(QObject *, int)));

	CHECKED_CONNECT(
		set, 
		SIGNAL(timingChanged(QObject *, int)), 
		this, 
		SLOT(setMercuryTiming(QObject *, int)));

	CHECKED_CONNECT(
		set, 
		SIGNAL(clientDisconnectedEvent(int)), 
		this, 
		SLOT(setClientDisconnected(int)));

	CHECKED_CONNECT(
		set, 
		SIGNAL(clientNoConnectedChanged(QObject*, int)), 
		this, 
		SLOT(setClientConnected(QObject*, int)));

	CHECKED_CONNECT(
		set, 
		SIGNAL(rxConnectedStatusChanged(QObject*, int, bool)), 
		this, 
		SLOT(setRxConnectedStatus(QObject*, int, bool)));

	CHECKED_CONNECT(
		set, 
		SIGNAL(audioRxChanged(QObject*, int)), 
		this, 
		SLOT(setAudioReceiver(QObject*, int)));

	CHECKED_CONNECT(
		set, 
		SIGNAL(framesPerSecondChanged(QObject*, int, int)),
		this, 
		SLOT(setFramesPerSecond(QObject*, int, int)));

	CHECKED_CONNECT(
		set, 
		SIGNAL(searchMetisSignal()), 
		this, 
		SLOT(searchHpsdrNetworkDevices()));

	/*CHECKED_CONNECT(
		set, 
		SIGNAL(spectrumAveragingChanged(QObject*, int, bool)), 
		this, 
		SLOT(setWbSpectrumAveraging(QObject*, int, bool)));*/

	CHECKED_CONNECT(
		set, 
		SIGNAL(networkDeviceNumberChanged(int)), 
		this, 
		SLOT(setHPSDRDeviceNumber(int)));

//	CHECKED_CONNECT(
//		set,
//		SIGNAL(alexConfigurationChanged(const QList<TAlexConfiguration> &)),
//		this,
//		SLOT(setAlexConfiguration(const QList<TAlexConfiguration> &)));

	CHECKED_CONNECT(
		set,
		SIGNAL(alexConfigurationChanged(quint16)),
		this,
		SLOT(setAlexConfiguration(quint16)));

	CHECKED_CONNECT(
		set,
		SIGNAL(alexStateChanged(HamBand, const QList<int> &)),
		this,
		SLOT(setAlexStates(HamBand, const QList<int> &)));

	CHECKED_CONNECT(
		set,
		SIGNAL(pennyOCEnabledChanged(bool)),
		this,
		SLOT(setPennyOCEnabled(bool)));

	CHECKED_CONNECT(
		set,
		SIGNAL(rxJ6PinsChanged(const QList<int> &)),
		this,
		SLOT(setRxJ6Pins(const QList<int> &)));

	CHECKED_CONNECT(
		set,
		SIGNAL(txJ6PinsChanged(const QList<int> &)),
		this,
		SLOT(setTxJ6Pins(const QList<int> &)));

	CHECKED_CONNECT(
		m_audioEngine,
		SIGNAL(formatChanged(QObject *, const QAudioFormat )), 
		set, 
		SLOT(setAudioFormat(QObject *, const QAudioFormat )));

	CHECKED_CONNECT(
		m_audioEngine,
		SIGNAL(formatChanged(QObject *, const QAudioFormat )), 
		this, 
		SLOT(setAudioFileFormat(QObject *, const QAudioFormat )));

	CHECKED_CONNECT(
		m_audioEngine, 
		SIGNAL(playPositionChanged(QObject *, qint64)),
		set,
		SLOT(setAudioPosition(QObject *, qint64)));

	CHECKED_CONNECT(
		m_audioEngine, 
		SIGNAL(playPositionChanged(QObject *, qint64)),
		this,
		SLOT(setAudioFilePosition(QObject *, qint64)));

	CHECKED_CONNECT(
		m_audioEngine, 
		SIGNAL(bufferChanged(QObject *, qint64, qint64, const QByteArray)),
		set,
		SLOT(setAudioBuffer(QObject *, qint64, qint64, const QByteArray)));

	CHECKED_CONNECT(
		m_audioEngine, 
		SIGNAL(bufferChanged(QObject *, qint64, qint64, const QByteArray)),
		this,
		SLOT(setAudioFileBuffer(QObject *, qint64, qint64, const QByteArray)));

	CHECKED_CONNECT(
		m_audioEngine, 
		SIGNAL(audiofileBufferChanged(const QList<qreal>)),
		this,
		SLOT(setAudioFileBuffer(const QList<qreal>)));
}
 
//********************************************************
// start/stop data engine
bool DataEngine::startDataEngineWithoutConnection() {

	DATA_ENGINE_DEBUG << "no HPSDR-HW interface";

	if (io.inputBuffer.length() > 0) {

		initReceivers(1);
		if (!m_dataIO)	createDataIO();
		if (!m_dataProcessor)	createDataProcessor();
		
		// data receiver thread
		if (!startDataIO(QThread::NormalPriority)) {

			setSystemState(QSDR::DataReceiverThreadError, m_hwInterface, m_serverMode, QSDR::DataEngineDown);
			return false;
		}

		switch (m_serverMode) {

			case QSDR::SDRMode:
			case QSDR::ChirpWSPR:
			case QSDR::NoServerMode:
			case QSDR::DemoMode:
				return false;

			case QSDR::ChirpWSPRFile:			

				if (!m_chirpInititalized) createChirpDataProcessor();				

				m_chirpProcessor->generateLocalChirp();
				
				if (!startChirpDataProcessor(QThread::NormalPriority)) {
					
					setSystemState(QSDR::ChirpDataProcessThreadError, m_hwInterface, m_serverMode, QSDR::DataEngineDown);
					return false;
				}
		
				m_chirpDspEngine = new QDSPEngine(this, 0, 2*BUFFER_SIZE);

				cpxIn.resize(2*BUFFER_SIZE);
				cpxOut.resize(2*BUFFER_SIZE);

				RX.at(0)->setConnectedStatus(true);
				set->setRxList(RX);

				m_rxSamples = 0;
				m_chirpSamples = 0;

				break;
		}

		// IQ data processing thread
		if (!startDataProcessor(QThread::NormalPriority)) {

			setSystemState(QSDR::DataProcessThreadError, m_hwInterface, m_serverMode, QSDR::DataEngineDown);
			return false;
		}
		setSystemState(QSDR::NoError, m_hwInterface, m_serverMode, QSDR::DataEngineUp);
		return true;
	}
	else {

		DATA_ENGINE_DEBUG << "no data available - data file loaded?";
		return false;
	}
}

bool DataEngine::findHPSDRDevices() {

	if (!m_discoverer) createDiscoverer();

	// HPSDR network IO thread
	if (!startDiscoverer(QThread::NormalPriority)) {

		io.networkIOMutex.lock();
		DATA_ENGINE_DEBUG << "HPSDR device discovery thread could not be started.";
		io.networkIOMutex.unlock();
		return false;
	}

	io.networkIOMutex.lock();
	DATA_ENGINE_DEBUG << "HPSDR network device detection...please wait.";
	set->setSystemMessage("HPSDR network device detection...please wait", 0);
	io.devicefound.wait(&io.networkIOMutex);

	m_hpsdrDevices = set->getHpsdrNetworkDevices();
	if (m_hpsdrDevices == 0) {

		io.networkIOMutex.unlock();
		stopDiscoverer();
		DATA_ENGINE_DEBUG << "no device found. HPSDR hardware powered? Network connection established?";
		set->setSystemMessage("no device found. HPSDR hardware powered? Network connection established?", 10000);

		setSystemState(QSDR::HwIOError,	m_hwInterface, m_serverMode, QSDR::DataEngineDown);
	}
	else {

		emit clearSystemMessageEvent();
		if (m_hpsdrDevices > 1)
			set->showNetworkIODialog();

		QList<TNetworkDevicecard> metisList = set->getMetisCardsList();
		DATA_ENGINE_DEBUG << "found " << metisList.count() << " network device(s)";
				
		for (int i = 0; i < metisList.count(); i++) {

			DATA_ENGINE_DEBUG 	<< "Device "
								<< i << " @ "
								<< qPrintable(metisList.at(i).ip_address.toString())
								//<< " [" << qPrintable((char *) &metisList.at(i).mac_address) << "]";
								<< " [" << metisList.at(i).mac_address << "]";
		}

		io.hpsdrDeviceIPAddress = set->getCurrentMetisCard().ip_address;
		io.hpsdrDeviceName = set->getCurrentMetisCard().boardName;
		DATA_ENGINE_DEBUG << "using HPSDR network device at " << qPrintable(io.hpsdrDeviceIPAddress.toString());

		//Sleep(100);
		SleeperThread::msleep(100);

		// stop the discovery thread
		io.networkIOMutex.unlock();
		stopDiscoverer();

		if (getFirmwareVersions()) return true;
		return false;
	}

	return false;
}

bool DataEngine::getFirmwareVersions() {

	m_fwCount = 0;

	// init receivers
	int rcvrs = set->getNumberOfReceivers();

	QString str = "Initializing %1 receiver(s)...please wait";
	set->setSystemMessage(str.arg(set->getNumberOfReceivers()), rcvrs * 500);

	if (!initReceivers(rcvrs)) return false;

	if (!m_dataIO) createDataIO();
		
	if (!m_dataProcessor) createDataProcessor();

	switch (m_serverMode) {

		case QSDR::SDRMode:
			
			for (int i = 0; i < set->getNumberOfReceivers(); i++) {

				RX.at(i)->setConnectedStatus(true);
			}
			setTimeStamp(this, false);
			break;

		default:

			DATA_ENGINE_DEBUG << "no valid server mode";
			setSystemState(QSDR::ServerModeError, m_hwInterface, m_serverMode, QSDR::DataEngineDown);

			return false;
	}

	set->setRxList(RX);
	connectDSPSlots();

	for (int i = 0; i < set->getNumberOfReceivers(); i++)
		RX.at(i)->setAudioVolume(this, i, 0.0f);

	// IQ data processing thread
	if (!startDataProcessor(QThread::NormalPriority)) {

		DATA_ENGINE_DEBUG << "data processor thread could not be started.";
		return false;
	}

	// data IO thread
	if (!startDataIO(QThread::NormalPriority)) {//  ::NormalPriority)) {

		DATA_ENGINE_DEBUG << "data IO thread could not be started.";
		return false;
	}

	//setSampleRate(this, set->getSampleRate());
	SleeperThread::msleep(100);

	// pre-conditioning
	for (int i = 0; i < io.receivers; i++)
		m_dataIO->sendInitFramesToNetworkDevice(i);
				
	if (m_serverMode == QSDR::SDRMode)
		m_dataIO->networkDeviceStartStop(0x01); // 0x01 for starting Metis without wide band data
		
	m_networkDeviceRunning = true;
	setSystemState(QSDR::NoError, m_hwInterface, m_serverMode, QSDR::DataEngineUp);
	SleeperThread::msleep(300);

	io.metisFW = set->getMetisVersion();
	io.mercuryFW = set->getMercuryVersion();
	io.penelopeFW = set->getPenelopeVersion();
	io.pennylaneFW = set->getPennyLaneVersion();
	io.hermesFW = set->getHermesVersion();

	// if we have 4096 * 16 bit = 8 * 1024 raw consecutive ADC samples, m_wbBuffers = 8
	// we have 16384 * 16 bit = 32 * 1024 raw consecutive ADC samples, m_wbBuffers = 32
	int wbBuffers = 0;
	if (io.mercuryFW > 32 || io.hermesFW > 11)
		wbBuffers = BIGWIDEBANDSIZE / 512;
	else
		wbBuffers = SMALLWIDEBANDSIZE / 512;

	set->setWidebandBuffers(this, wbBuffers);

	if (set->getFirmwareVersionCheck())
		return checkFirmwareVersions();
	else
		return true;
}

// credits go to George Byrkit, K9TRV: the older FW checkings are shamelessly taken from the KISS Konsole!
bool DataEngine::checkFirmwareVersions() {

	if (io.metisFW != 0 &&  io.hpsdrDeviceName == "Hermes") {

		stop();

		QString msg = "Metis selected, but Hermes found!";
		set->showWarningDialog(msg);
		return false;
	}

	if (io.hermesFW != 0 && io.hpsdrDeviceName == "Metis") {

		stop();

		QString msg = "Hermes selected, but Metis found!";
		set->showWarningDialog(msg);
		return false;
	}

	if (io.penelopeFW == 0 && (set->getPenelopePresence() || set->getPennyLanePresence())) {

		stop();

		QString msg = "Penelope or Pennylane selected, but firmware version = 0 !";
		set->showWarningDialog(msg);
		return false;
	}

	if (io.mercuryFW < 27 && set->getNumberOfReceivers() > 4 && io.hpsdrDeviceName == "Metis") {

		stop();

		QString msg = "Mercury FW must be V2.7 or higher!";
		set->showWarningDialog(msg);
		return false;
	}

	if (io.hpsdrDeviceName == "Metis") {

		QString msg;
		switch (io.metisFW) {

			case 13:
				if (((set->getPenelopePresence() || set->getPennyLanePresence()) &&
					(io.penelopeFW == 13 || io.pennylaneFW == 13)) ||
					io.mercuryFW != 29)
				{
					stop();

					msg = "Penny[Lane] FW Version V1.3 and Mercury FW V2.7 requires Metis FW V1.3!";
					set->showWarningDialog(msg);
					return false;
				}
				break;

			case 14:
				if (((set->getPenelopePresence() || set->getPennyLanePresence()) &&
					(io.penelopeFW == 14 || io.pennylaneFW == 14)) ||
					io.mercuryFW != 29)
				{
					stop();

					msg = "Penny[Lane] FW Version V1.4 and Mercury FW V2.7 requires Metis FW V1.4!";
					set->showWarningDialog(msg);
					return false;
				}
				break;

			case 15:

				if (((set->getPenelopePresence() || set->getPennyLanePresence()) &&
					(io.penelopeFW == 15 || io.pennylaneFW == 15)) ||
					io.mercuryFW != 30)
				{
					stop();

					msg = "Penny[Lane] FW Version V1.5 and Mercury FW V3.0 requires Metis FW V1.5!";
					set->showWarningDialog(msg);
					return false;
				}
				break;

			case 16:

				if (((set->getPenelopePresence() || set->getPennyLanePresence()) &&
					(io.penelopeFW == 16 || io.pennylaneFW == 16)) ||
					io.mercuryFW != 31)
				{
					stop();

					msg = "Penny[Lane] FW Version V1.6 and Mercury FW V3.1 requires Metis FW V1.6!";
					set->showWarningDialog(msg);
					return false;
				}
				break;

			case 17:
			case 18:

				if (((set->getPenelopePresence() || set->getPennyLanePresence()) &&
					(io.penelopeFW == 17 || io.pennylaneFW == 17)) ||
					io.mercuryFW != 32)
				{
					stop();

					msg = "Penny[Lane] FW Version V1.7 and Mercury FW V3.2 requires Metis FW V1.7 or V1.8!";
					set->showWarningDialog(msg);
					return false;
				}
				break;

			case 19:
			case 20:

				stop();

				msg = "Metis FW V1.9 or V2.0 have some problems - please upgrade to Metis V2.1!";
				set->showWarningDialog(msg);
				return false;
				break;

			case 21:

				if ((set->getPenelopePresence() && io.penelopeFW != 17)	||
					(set->getPennyLanePresence() && io.pennylaneFW != 17)||
					io.mercuryFW != 33)
				{
					stop();

					msg = "Penny[Lane] FW Version V1.7 and Mercury FW V3.3 required for Metis FW V2.1!";
					set->showWarningDialog(msg);
					return false;
				}
				break;

//			case 22:
//
//				if ((set->getPenelopePresence() && m_penelopeFW != 17)	||
//					(set->getPennyLanePresence() && m_pennylaneFW != 17)||
//					m_mercuryFW != 33)
//				{
//					stop();
//
//					msg = "Penny[Lane] FW Version V1.7 and Mercury FW V3.3 required for Metis FW >= V2.1!";
//					set->showWarningDialog(msg);
//					return false;
//				}
//				break;

            case 26:

                if ((set->getPenelopePresence() && io.penelopeFW != 18)	||
                    (set->getPennyLanePresence() && io.pennylaneFW != 18)||
                    io.mercuryFW != 34)
                {
                    stop();

                    msg = "Penny[Lane] FW Version V1.8 and Mercury FW V3.4 required for Metis FW V2.6!";
                    set->showWarningDialog(msg);
                    return false;
                }
                break;

            default:

				//stop();

				msg = "Not a standard Metis FW version !";
				set->showWarningDialog(msg);
				//return false;
				return true;
		}
	}

	if (io.mercuryFW < 33 && set->getNumberOfReceivers() > 4 && io.hpsdrDeviceName == "Metis") {

		stop();

		QString msg = "Mercury FW < V3.3 has only 4 receivers!";
		set->showWarningDialog(msg);
		return false;
	}

	if (io.hermesFW < 18 && set->getNumberOfReceivers() > 2 && io.hpsdrDeviceName == "Hermes") {

		stop();

		QString msg = "Hermes FW < V1.8 has only 2 receivers!";
		set->showWarningDialog(msg);
		return false;
	}

	return true;
}

bool DataEngine::start() {

	m_fwCount = 0;
	m_sendState = 0;

	int rcvrs = set->getNumberOfReceivers();
	if (!initReceivers(rcvrs)) return false;

	if (!m_dataIO) createDataIO();
	
	if (!m_dataProcessor) createDataProcessor();
		
	if (m_serverMode == QSDR::SDRMode && !m_wbDataProcessor)
		createWideBandDataProcessor();

	if ((m_serverMode == QSDR::ChirpWSPR) && !m_chirpProcessor)
		createChirpDataProcessor();
		
	switch (m_serverMode) {

		//case QSDR::ExternalDSP:
		
			/*
			//CHECKED_CONNECT(
			//	set,
			//	SIGNAL(frequencyChanged(QObject*, bool, int, long)),
			//	this,
			//	SLOT(setFrequency(QObject*, bool, int, long)));

			//if (!m_audioProcessorRunning) {

			//	//if (!m_audioProcessor)	createAudioProcessor();
			//	if (!m_audioReceiver)	createAudioReceiver();

			//	m_audioInProcThread->start();
			//	if (m_audioInProcThread->isRunning()) {
			//
			//		m_audioInProcThreadRunning = true;
			//		DATA_ENGINE_DEBUG << "Audio processor process started.";
			//	}
			//	else {

			//		m_audioInProcThreadRunning = false;
			//		setSystemState(
			//						QSDR::AudioThreadError,
			//						m_hwInterface,
			//						m_serverMode,
			//						QSDR::DataEngineDown);
			//		return false;
			//	}
			//
			//	io.audio_rx = 0;
			//	io.clientList.append(0);

			//	m_audioProcessorRunning = true;
			//	setSystemState(
			//			QSDR::NoError,
			//			m_hwInterface,
			//			m_serverMode,
			//			QSDR::DataEngineUp);
			//}
			 */
			//return false;

		case QSDR::SDRMode:
			
			setTimeStamp(this, false);
			break;

		case QSDR::ChirpWSPR:
		//case QSDR::ChirpWSPRFile:

			// turn time stamping on
			setTimeStamp(this, true);

			if (!startChirpDataProcessor(QThread::NormalPriority)) {

				DATA_ENGINE_DEBUG << "data processor thread could not be started.";
				return false;
			}

			//RX.at(0)->setConnectedStatus(true);

			CHECKED_CONNECT(
					set,
					SIGNAL(ctrFrequencyChanged(QObject *, int, int, long)),
					this,
					SLOT(setFrequency(QObject *, int, int, long)));

			break;

		default:

			DATA_ENGINE_DEBUG << "no valid server mode";

			setSystemState(QSDR::ServerModeError, m_hwInterface, m_serverMode, QSDR::DataEngineDown);
			return false;
	}	// end switch (m_serverMode)

	set->setRxList(RX);
	connectDSPSlots();

	for (int i = 0; i < rcvrs; i++) {

		RX.at(i)->setConnectedStatus(true);
		RX.at(i)->setAudioVolume(this, i, RX.at(i)->getAudioVolume());
		setFrequency(this, true, i, set->getCtrFrequencies().at(i));

		//CHECKED_CONNECT(
		//		RX.at(i),
		//		SIGNAL(outputBufferSignal(int, const CPX &)),
		//		this, //m_dataProcessor,
		//		SLOT(setOutputBuffer(int, const CPX &)));

		CHECKED_CONNECT(
				RX.at(i),
				SIGNAL(outputBufferSignal(int, const CPX &)),
				m_dataProcessor,
				SLOT(setOutputBuffer(int, const CPX &)));

		m_dspThreadList.at(i)->start(QThread::NormalPriority);//QThread::TimeCriticalPriority);
				
		if (m_dspThreadList.at(i)->isRunning()) {
					
			//m_dataProcThreadRunning = true;
			io.networkIOMutex.lock();
			DATA_ENGINE_DEBUG << "receiver processor thread started for Rx " << i;
			io.networkIOMutex.unlock();
		}
		else {

			//m_dataProcThreadRunning = false;
			//setSystemState(QSDR::DataProcessThreadError, m_hwInterface, m_serverMode, QSDR::DataEngineDown);
			return false;
	}
	}

	if (m_serverMode != QSDR::ChirpWSPR && !startWideBandDataProcessor(QThread::NormalPriority)) {

		DATA_ENGINE_DEBUG << "wide band data processor thread could not be started.";
		return false;
	}

	// data IO thread
//	if (!startDataIO(QThread::HighPriority)) {//  ::NormalPriority)) {
//
//		DATA_ENGINE_DEBUG << "data receiver thread could not be started.";
//		return false;
//	}

	// IQ data processing thread
	if (!startDataProcessor(QThread::NormalPriority)) {

		DATA_ENGINE_DEBUG << "data processor thread could not be started.";
		return false;
	}

	// data IO thread
	if (!startDataIO(QThread::NormalPriority)) {//  ::NormalPriority::HighPriority)) {

		DATA_ENGINE_DEBUG << "data IO thread could not be started.";
		return false;
	}
	
	// start Sync,ADC and S-Meter timers
	//m_SyncChangedTime.start();
	//m_ADCChangedTime.start();
	//m_smeterTime.start();

	// start the "frames-per-second" timer for all receivers
	for (int i = 0; i < rcvrs; i++)
		RX.at(i)->highResTimer->start();

	// just give them a little time..
	SleeperThread::msleep(100);

	// pre-conditioning
	for (int i = 0; i < io.receivers; i++)
		m_dataIO->sendInitFramesToNetworkDevice(i);
				
	if (m_serverMode == QSDR::SDRMode && set->getWidebandData())
		m_dataIO->networkDeviceStartStop(0x03); // 0x03 for starting the device with wide band data
	else
		m_dataIO->networkDeviceStartStop(0x01); // 0x01 for starting the device without wide band data
		
	m_networkDeviceRunning = true;

	setSystemState(QSDR::NoError, m_hwInterface, m_serverMode, QSDR::DataEngineUp);
	set->setSystemMessage("System running", 4000);

	DATA_ENGINE_DEBUG << "Data Engine thread: " << this->thread();

	return true;
}

void DataEngine::stop() {

	if (m_dataEngineState == QSDR::DataEngineUp) {
		
		switch (m_hwInterface) {

			case QSDR::Metis:
			case QSDR::Hermes:
				
				// turn time stamping off
				setTimeStamp(this, false);

				// stop the device
				m_dataIO->networkDeviceStartStop(0);
				m_networkDeviceRunning = false;
				DATA_ENGINE_DEBUG << "HPSDR device stopped";

				// stop the threads
				//SleeperThread::msleep(100);
				stopDataIO();
				SleeperThread::msleep(100);
				stopDataProcessor();
				//stopChirpDataProcessor();
				if (m_wbDataProcessor)
					stopWideBandDataProcessor();
				
				// clear device list
				SleeperThread::msleep(100);
				set->clearMetisCardList();
				DATA_ENGINE_DEBUG << "device cards list cleared.";
				break;

			case QSDR::NoInterfaceMode:

				stopDataIO();
				
				DATA_ENGINE_DEBUG << "data queue count: " << io.data_queue.count();
				DATA_ENGINE_DEBUG << "chirp queue count: " << io.chirp_queue.count();

				stopDataProcessor();
				stopChirpDataProcessor();
		}

		while (!io.au_queue.isEmpty())
			io.au_queue.dequeue();

		// clear receiver thread list
		foreach (QThread* thread, m_dspThreadList) {

			thread->quit();
			thread->wait();
		}
		qDeleteAll(m_dspThreadList.begin(), m_dspThreadList.end());
		m_dspThreadList.clear();

		// clear receiver list
		foreach (Receiver *rx, RX) {

			rx->stop();
			rx->setConnectedStatus(false);
			disconnectDSPSlots();

			disconnect(
				rx,
				SIGNAL(spectrumBufferChanged(int, const qVectorFloat&)),
				set,
				SLOT(setSpectrumBuffer(int, const qVectorFloat&)));

			disconnect(
				rx,
				SIGNAL(sMeterValueChanged(int, float)),
				set,
				SLOT(setSMeterValue(int, float)));

			/*disconnect(
				rx,
				SIGNAL(outputBufferSignal(int, const CPX &)),
				this,
				SLOT(setOutputBuffer(int, const CPX &)));*/

			/*disconnect(
				rx,
				SIGNAL(outputBufferSignal(int, const CPX &)),
				m_dataProcessor,
				SLOT(setOutputBuffer(int, const CPX &)));*/

			//rx->deleteDSPInterface();
			//DATA_ENGINE_DEBUG << "DSP core deleted.";
		}
		qDeleteAll(RX.begin(), RX.end());
		RX.clear();
		set->setRxList(RX);
		DATA_ENGINE_DEBUG << "receiver threads deleted, receivers deleted, receiver & thread list cleared.";
		set->setSystemMessage("Data engine shut down.", 4000);

		setSystemState(QSDR::NoError, m_hwInterface, m_serverMode, QSDR::DataEngineDown);
	}

	m_rxSamples = 0;
	m_chirpSamples = 0;
	m_restart = true;
	m_found = 0;
	m_hpsdrDevices = 0;

	set->setMercuryVersion(0);
	set->setPenelopeVersion(0);
	set->setMetisVersion(0);
	set->setHermesVersion(0);

	//set->setPeakHold(false);
	//set->resetWidebandSpectrumBuffer();

	/*disconnect(
		set, 
		SIGNAL(ctrFrequencyChanged(QObject*, int, int, long)), 
		this, 
		SLOT(setFrequency(QObject*, int, int, long)));*/

	DATA_ENGINE_DEBUG << "shut down done.";
}

bool DataEngine::initDataEngine() {

#ifdef TESTING
	qDebug() << "************************** TESTING MODUS ***********************************";
	return start();
#endif

	if (m_hwInterface == QSDR::NoInterfaceMode) {
		
		return startDataEngineWithoutConnection();
	}
	else {
		
		if (findHPSDRDevices()) {
		
			if (io.mercuryFW > 0 || io.hermesFW > 0) {
				
				stop();
				DATA_ENGINE_DEBUG << "got firmware versions:";
				DATA_ENGINE_DEBUG << "	Metis firmware:  " << io.metisFW;
				DATA_ENGINE_DEBUG << "	Mercury firmware:  " << io.mercuryFW;
				DATA_ENGINE_DEBUG << "	Penelope firmware:  " << io.penelopeFW;
				DATA_ENGINE_DEBUG << "	Pennylane firmware:  " << io.pennylaneFW;
				DATA_ENGINE_DEBUG << "	Hermes firmware: " << io.hermesFW;
				DATA_ENGINE_DEBUG << "stopping and restarting data engine.";

				return start();
			}
			else {

				DATA_ENGINE_DEBUG << "did not get firmware versions!";
				setSystemState(QSDR::FirmwareError, m_hwInterface, m_serverMode, QSDR::DataEngineDown);
			}
		}
	}
	return false;
}

bool DataEngine::initReceivers(int rcvrs) {
		
	for (int i = 0; i < rcvrs; i++) {
			
		Receiver *rx = new Receiver(i);

		// init the DSP core
		DATA_ENGINE_DEBUG << "trying to init a DSP core for rx " << i;

		if (rx->initDSPInterface()) {

			DATA_ENGINE_DEBUG << "init DSP core for rx " << i << " successful !";

			rx->setConnectedStatus(false);
			rx->setServerMode(m_serverMode);

			// create dsp thread
			QThreadEx* thread = new QThreadEx();
			rx->moveToThread(thread);

			//CHECKED_CONNECT(this, SIGNAL(doDSP()), rx, SLOT(dspProcessing()));
			
			CHECKED_CONNECT(
				rx,
				SIGNAL(spectrumBufferChanged(int, const qVectorFloat&)),
				set,
				SLOT(setSpectrumBuffer(int, const qVectorFloat&)));

			CHECKED_CONNECT(
				rx,
				SIGNAL(sMeterValueChanged(int, float)),
				set,
				SLOT(setSMeterValue(int, float)));

			/*CHECKED_CONNECT(
				rx,
				SIGNAL(outputBufferSignal(int, const CPX &)),
				m_dataProcessor,
				SLOT(setOutputBuffer(int, const CPX &)));*/
			
			m_dspThreadList.append(thread);
			RX.append(rx);
		}
		else {

			return false;
		}
    }

	set->setRxList(RX);

	m_txFrame = 0;
	
	io.currentReceiver = 0;
	io.receivers = rcvrs;

	io.timing = 0;
	m_configure = io.receivers + 1;

	// init cc Rc parameters
	io.ccRx.devices.mercuryFWVersion = 0;
	io.ccRx.devices.penelopeFWVersion = 0;
	io.ccRx.devices.pennylaneFWVersion = 0;
	io.ccRx.devices.hermesFWVersion = 0;
	io.ccRx.devices.metisFWVersion = 0;

	io.ccRx.ptt    = false;
	io.ccRx.dash   = false;
	io.ccRx.dot    = false;
	io.ccRx.lt2208 = false;
	io.ccRx.ain1   = 0;
	io.ccRx.ain2   = 0;
	io.ccRx.ain3   = 0;
	io.ccRx.ain4   = 0;
	io.ccRx.ain5   = 0;
	io.ccRx.ain6   = 0;
	io.ccRx.hermesI01 = false;
	io.ccRx.hermesI02 = false;
	io.ccRx.hermesI03 = false;
	io.ccRx.hermesI04 = false;
	io.ccRx.mercury1_LT2208 = false;
	io.ccRx.mercury2_LT2208 = false;
	io.ccRx.mercury3_LT2208 = false;
	io.ccRx.mercury4_LT2208 = false;

	// init cc Tx parameters
	io.ccTx.currentBand = RX.at(0)->getHamBand();
	io.ccTx.mercuryAttenuators = RX.at(0)->getMercuryAttenuators();
	io.ccTx.mercuryAttenuator = RX.at(0)->getMercuryAttenuators().at(io.ccTx.currentBand);
	io.ccTx.dither = set->getMercuryDither();
	io.ccTx.random = set->getMercuryRandom();
	io.ccTx.duplex = 1;
	io.ccTx.mox = false;
	io.ccTx.ptt = false;
	io.ccTx.alexStates = set->getAlexStates();
	io.ccTx.vnaMode = false;
	io.ccTx.alexConfig = set->getAlexConfig();
	io.ccTx.timeStamp = 0;
	io.ccTx.commonMercuryFrequencies = 0;
	io.ccTx.pennyOCenabled = set->getPennyOCEnabled();
	io.ccTx.rxJ6pinList = set->getRxJ6Pins();
	io.ccTx.txJ6pinList = set->getTxJ6Pins();

	setAlexConfiguration(io.ccTx.alexConfig);

	io.rxClass = set->getRxClass();
	io.mic_gain = 0.26F;
	io.rx_freq_change = -1;
	io.tx_freq_change = -1;
	io.clients = 0;
	io.sendIQ_toggle = true;
	io.rcveIQ_toggle = false;
	io.alexForwardVolts = 0.0;
	io.alexReverseVolts = 0.0;
	io.alexForwardPower = 0.0;
	io.alexReversePower = 0.0;
	io.penelopeForwardVolts = 0.0;
	io.penelopeForwardPower = 0.0;
	io.ain3Volts = 0.0;
	io.ain4Volts = 0.0;
	io.supplyVolts = 0.0f;


	//*****************************
	// C&C bytes
	for (int i = 0; i < 5; i++) {

		io.control_in[i]  = 0x00;
		io.control_out[i] = 0x00;
	}

	// C0
	// 0 0 0 0 0 0 0 0
	//               |
	//               +------------ MOX (1 = active, 0 = inactive)

	io.control_out[0] |= MOX_DISABLED;

	// set C1
	//
	// 0 0 0 0 0 0 0 0
	// | | | | | | | |
	// | | | | | | + +------------ Speed (00 = 48kHz, 01 = 96kHz, 10 = 192kHz)
	// | | | | + +---------------- 10MHz Ref. (00 = Atlas/Excalibur, 01 = Penelope, 10 = Mercury)*
	// | | | +-------------------- 122.88MHz source (0 = Penelope, 1 = Mercury)*
	// | + +---------------------- Config (00 = nil, 01 = Penelope, 10 = Mercury, 11 = both)*
	// +-------------------------- Mic source (0 = Janus, 1 = Penelope)*

	// Bits 1,0
	setSampleRate(this, set->getSampleRate());

	// Bits 7,..,2
	setHPSDRConfig();
	
	io.control_out[1] &= 0x03; // 0 0 0 0 0 0 1 1
	io.control_out[1] |= io.ccTx.clockByte;

	// set C2
	//
	// 0 0 0 0 0 0 0 0
	// |           | |
	// |           | +------------ Mode (1 = Class E, 0 = All other modes)
    // +---------- +-------------- Open Collector Outputs on Penelope or Hermes (bit 6..bit 0)

	io.control_out[2] = io.control_out[2] & 0xFE; // 1 1 1 1 1 1 1 0
	io.control_out[2] = io.control_out[2] | io.rxClass;

	// set C3
	//
	// 0 0 0 0 0 0 0 0
	// | | | | | | | |
	// | | | | | | + +------------ Alex Attenuator (00 = 0dB, 01 = 10dB, 10 = 20dB, 11 = 30dB)
	// | | | | | +---------------- Preamp On/Off (0 = Off, 1 = On)
	// | | | | +------------------ LT2208 Dither (0 = Off, 1 = On)
	// | | | + ------------------- LT2208 Random (0= Off, 1 = On)
	// | + + --------------------- Alex Rx Antenna (00 = none, 01 = Rx1, 10 = Rx2, 11 = XV)
	// + ------------------------- Alex Rx out (0 = off, 1 = on). Set if Alex Rx Antenna > 00.

	io.control_out[3] = io.control_out[3] & 0xFB; // 1 1 1 1 1 0 1 1
	io.control_out[3] = io.control_out[3] | (io.ccTx.mercuryAttenuator << 2);

	io.control_out[3] = io.control_out[3] & 0xF7; // 1 1 1 1 0 1 1 1
	io.control_out[3] = io.control_out[3] | (io.ccTx.dither << 3);

	io.control_out[3] = io.control_out[3] & 0xEF; // 1 1 1 0 1 1 1 1
	io.control_out[3] = io.control_out[3] | (io.ccTx.random << 4);

	// set C4
	//
	// 0 0 0 0 0 0 0 0
	// | | | | | | | |
	// | | | | | | + + ----------- Alex Tx relay (00 = Tx1, 01= Tx2, 10 = Tx3)
	// | | | | | + --------------- Duplex (0 = off, 1 = on)
	// + + + + +------------------ Number of Receivers (00000 = 1, 11111 = 32)

	//RRK removed 4HL
        // | +------------------------ Time stamp - 1PPS on LSB of Mic data (0 = off, 1 = on)
	// +-------------------------- Common Mercury Frequency (0 = independent frequencies to Mercury
	//			                   Boards, 1 = same frequency to all Mercury boards)

	io.control_out[4] &= 0x07; // 1 1 0 0 0 1 1 1
	io.control_out[4] = (io.ccTx.duplex << 2) | ((io.receivers - 1) << 3);

	return true;
}

void DataEngine::setHPSDRConfig() {

	io.ccTx.clockByte = 0x0;

	// C1
	// 0 0 0 0 0 0 0 0
	// | | | | | | | |
	// | | | | | | + +------------ Speed (00 = 48kHz, 01 = 96kHz, 10 = 192kHz)
	// | | | | + +---------------- 10MHz Ref. (00 = Atlas/Excalibur, 01 = Penelope, 10 = Mercury)*
	// | | | +-------------------- 122.88MHz source (0 = Penelope, 1 = Mercury)*
	// | + +---------------------- Config (00 = nil, 01 = Penelope, 10 = Mercury, 11 = both)*
	// +-------------------------- Mic source (0 = Janus, 1 = Penelope)*
	//
	// * Ignored by Hermes

	if (
		(set->getPenelopePresence()   || set->getPennyLanePresence()) &&
		((set->get10MHzSource() == 0) || set->getExcaliburPresence())
		)
	{

		io.ccTx.clockByte = MIC_SOURCE_PENELOPE | MERCURY_PRESENT | PENELOPE_PRESENT | MERCURY_122_88MHZ_SOURCE | ATLAS_10MHZ_SOURCE;
	}
	else if ((set->getPenelopePresence() || set->getPennyLanePresence()) && (set->get10MHzSource() == 1)) {
		
		io.ccTx.clockByte = MIC_SOURCE_PENELOPE | MERCURY_PRESENT | PENELOPE_PRESENT | MERCURY_122_88MHZ_SOURCE | PENELOPE_10MHZ_SOURCE;
	}
	else if ((set->getPenelopePresence() || set->getPennyLanePresence()) && (set->get10MHzSource() == 2)) {
		
		io.ccTx.clockByte = MIC_SOURCE_PENELOPE | MERCURY_PRESENT | PENELOPE_PRESENT | MERCURY_122_88MHZ_SOURCE | MERCURY_10MHZ_SOURCE;
	}
	else if ((set->get10MHzSource() == 0) || set->getExcaliburPresence()) {
		
		io.ccTx.clockByte = MERCURY_PRESENT | MERCURY_122_88MHZ_SOURCE | ATLAS_10MHZ_SOURCE;
	}
	else {
		
		io.ccTx.clockByte = MERCURY_PRESENT | MERCURY_122_88MHZ_SOURCE | MERCURY_10MHZ_SOURCE;
	}
}

void DataEngine::connectDSPSlots() {

	CHECKED_CONNECT(
		set,
		SIGNAL(ctrFrequencyChanged(QObject *, int, int, long)),
		this,
		SLOT(setFrequency(QObject *, int, int, long)));
}

void DataEngine::disconnectDSPSlots() {

	disconnect(
		set,
		SIGNAL(ctrFrequencyChanged(QObject *, int, int, long)),
		this,
		SLOT(setFrequency(QObject *, int, int, long)));
}

//********************************************************
// create, start/stop HPSDR device network IO

void DataEngine::createDiscoverer() {

	m_discoverer = new Discoverer(&io);

	m_discoveryThread = new QThreadEx();
	m_discoverer->moveToThread(m_discoveryThread);

	m_discoverer->connect(
					m_discoveryThread,
					SIGNAL(started()), 
					SLOT(initHPSDRDevice()));
}

bool DataEngine::startDiscoverer(QThread::Priority prio) {

	m_discoveryThread->start(prio);

	if (m_discoveryThread->isRunning()) {
					
		m_discoveryThreadRunning = true;
		io.networkIOMutex.lock();
		qDebug() << "";
		DATA_ENGINE_DEBUG << "HPSDR device discovery thread started.";
		io.networkIOMutex.unlock();

		return true;
	}
	else {

		m_discoveryThreadRunning = false;
		return false;
	}
}

void DataEngine::stopDiscoverer() {

	if (m_discoveryThread->isRunning()) {
		
		m_discoveryThread->quit();
		m_discoveryThread->wait(1000);
		delete m_discoveryThread;
		delete m_discoverer;
		m_discoverer = 0;

		m_discoveryThreadRunning = false;

		DATA_ENGINE_DEBUG << "HPSDR discovery thread stopped and deleted.";
	}
	else
		DATA_ENGINE_DEBUG << "HPSDR discovery thread wasn't started.";
}

//********************************************************
// create, start/stop data receiver

void DataEngine::createDataIO() {

	m_dataIO = new DataIO(&io);

	switch (m_serverMode) {
		
		//case QSDR::ExternalDSP:
		//	break;

		//case QSDR::InternalDSP:
		case QSDR::SDRMode:

			io.networkIOMutex.lock();
			DATA_ENGINE_DEBUG 	<< "configured for "
								<< qPrintable(QString::number(set->getNumberOfReceivers()))
								<< " receiver(s) at "
								<< qPrintable(QString::number(set->getSampleRate()/1000))
								<< " kHz sample rate";
			io.networkIOMutex.unlock();
//			sendMessage(
//				m_message.arg(
//					QString::number(set->getNumberOfReceivers()),
//					QString::number(set->getSampleRate()/1000)));
			break;

		case QSDR::ChirpWSPR:
		case QSDR::ChirpWSPRFile:
			break;
			
		case QSDR::NoServerMode:
		case QSDR::DemoMode:
			break;
	}

	m_dataIOThread = new QThreadEx();
	m_dataIO->moveToThread(m_dataIOThread);

	switch (m_hwInterface) {

		case QSDR::NoInterfaceMode:

			m_dataIO->connect(
						m_dataIOThread,
						SIGNAL(started()), 
						SLOT(readData()));
			break;
			
		case QSDR::Metis:
		case QSDR::Hermes:

			m_dataIO->connect(
						m_dataIOThread,
						SIGNAL(started()), 
						SLOT(initDataReceiverSocket()));
			break;
	}
}

bool DataEngine::startDataIO(QThread::Priority prio) {

	m_dataIOThread->start(prio);

	if (m_dataIOThread->isRunning()) {
					
		m_dataIOThreadRunning = true;
		io.networkIOMutex.lock();
		DATA_ENGINE_DEBUG << "data IO thread started.";
		io.networkIOMutex.unlock();

		return true;
	}
	else {

		m_dataIOThreadRunning = false;
		setSystemState(QSDR::DataProcessThreadError, m_hwInterface, m_serverMode, QSDR::DataEngineDown);
		return false;
	}
}

void DataEngine::stopDataIO() {

	if (m_dataIOThread->isRunning()) {
					
		m_dataIO->stop();
		m_dataIOThread->quit();

		while (!m_dataIOThread->isFinished()) {
		
			DATA_ENGINE_DEBUG << "data IO thread not yet finished...";
			if (m_dataIOThread->wait(100)) break;
		}
		m_dataIOThreadRunning = false;
		
		delete m_dataIOThread;
		delete m_dataIO;
		m_dataIO = 0;
		
		if (m_serverMode == QSDR::ChirpWSPRFile) {

			while (!io.chirp_queue.isEmpty())
				io.chirp_queue.dequeue();
		}

		DATA_ENGINE_DEBUG << "data IO thread deleted.";
	}
	else
		DATA_ENGINE_DEBUG << "data IO thread wasn't started.";
}
 
//********************************************************
// create, start/stop data processor

void DataEngine::createDataProcessor() {

	m_dataProcessor = new DataProcessor(this, m_serverMode, m_hwInterface);
	sendSocket = new QUdpSocket();

	CHECKED_CONNECT(
			sendSocket,
			SIGNAL(error(QAbstractSocket::SocketError)), 
			m_dataProcessor, 
			SLOT(displayDataProcessorSocketError(QAbstractSocket::SocketError)));

	switch (m_serverMode) {
		
		// The signal iqDataReady is generated by the function
		// processInputBuffer when a block of input data are
		// decoded.

		case QSDR::SDRMode:
		case QSDR::ChirpWSPR:
		case QSDR::ChirpWSPRFile:

			/*connect(
				this,
				SIGNAL(iqDataReady(int)),
				SLOT(dttSPDspProcessing(int)),
				Qt::DirectConnection);*/
			
			break;
			
		case QSDR::NoServerMode:
		case QSDR::DemoMode:
			break;

		/*
		case QSDR::ExternalDSP:
		
			CHECKED_CONNECT_OPT(
						this,
						SIGNAL(iqDataReady(int)),
						m_dataProcessor,
						SLOT(externalDspProcessing(int)),
						Qt::DirectConnection);
		
			break;

		case QSDR::ChirpWSPR:
		case QSDR::ChirpWSPRFile:
			break;
		*/
	}

	m_dataProcThread = new QThreadEx();
	m_dataProcessor->moveToThread(m_dataProcThread);
	sendSocket->moveToThread(m_dataProcThread);

	switch (m_hwInterface) {

		case QSDR::NoInterfaceMode:
			m_dataProcessor->connect(
						m_dataProcThread, 
						SIGNAL(started()), 
						SLOT(processData()));
			break;
			
		case QSDR::Metis:
		case QSDR::Hermes:
			m_dataProcessor->connect(
						m_dataProcThread, 
						SIGNAL(started()), 
						SLOT(processDeviceData()));

			break;
	}
	
	//m_dataProcessor->connect(m_dataProcThread, SIGNAL(started()), SLOT(initDataProcessorSocket()));
}

bool DataEngine::startDataProcessor(QThread::Priority prio) {

	m_dataProcThread->start(prio);
				
	if (m_dataProcThread->isRunning()) {
					
		m_dataProcThreadRunning = true;
		io.networkIOMutex.lock();
		DATA_ENGINE_DEBUG << "data processor thread started.";
		io.networkIOMutex.unlock();

		return true;
	}
	else {

		m_dataProcThreadRunning = false;
		setSystemState(QSDR::DataProcessThreadError, m_hwInterface, m_serverMode, QSDR::DataEngineDown);
		return false;
	}
}

void DataEngine::stopDataProcessor() {

	if (m_dataProcThread->isRunning()) {
					
		m_dataProcessor->stop();
		
		if (m_serverMode == QSDR::SDRMode || m_serverMode == QSDR::ChirpWSPR) {
			
			if (io.iq_queue.isEmpty()) {
				io.iq_queue.enqueue(QByteArray(BUFFER_SIZE, 0x0));
			}
		}
		else if (m_serverMode == QSDR::ChirpWSPRFile) {

			if (io.data_queue.isEmpty()) {
				
				QList<qreal> buf;
				for (int i = 0; i < 128; i++) buf << 0.0f;
				io.data_queue.enqueue(buf);
			}
		}
				
		m_dataProcThread->quit();
		m_dataProcThread->wait();
		delete m_dataProcThread;
		delete m_dataProcessor;
		m_dataProcessor = 0;

		if (m_serverMode == QSDR::SDRMode || m_serverMode == QSDR::ChirpWSPR) {

			while (!io.iq_queue.isEmpty())
				io.iq_queue.dequeue();

			DATA_ENGINE_DEBUG << "iq_queue empty.";
		}
		else if (m_serverMode == QSDR::ChirpWSPRFile) {
			
			while (!io.data_queue.isEmpty())
				io.data_queue.dequeue();

			DATA_ENGINE_DEBUG << "data_queue empty.";
			chirpData.clear();
		}

		m_dataProcThreadRunning = false;

		DATA_ENGINE_DEBUG << "data processor thread deleted.";
	}
	else
		DATA_ENGINE_DEBUG << "data processor thread wasn't started.";
}

//********************************************************
// create, start/stop audio out processor

void DataEngine::createAudioOutProcessor() {

	m_audioOutProcessor = new AudioOutProcessor(this, m_serverMode);

	switch (m_serverMode) {
		
		//case QSDR::ExternalDSP:
		//	break;

		case QSDR::SDRMode:
		
			/*connect(
				this,
				SIGNAL(iqDataReady(int)),
				SLOT(dttSPDspProcessing(int)),
				Qt::DirectConnection);*/
			
			break;
			
		case QSDR::NoServerMode:
		case QSDR::DemoMode:
			break;

		default:
			break;
	}

	m_audioOutProcThread = new QThreadEx();
	m_audioOutProcessor->moveToThread(m_audioOutProcThread);

	switch (m_hwInterface) {

		case QSDR::NoInterfaceMode:
			/*m_audioOutProcessor->connect(
									m_audioOutProcThread, 
									SIGNAL(started()), 
									SLOT(processData()));*/
			break;
			
		case QSDR::Metis:
		case QSDR::Hermes:
			/*m_audioOutProcessor->connect(
									m_audioOutProcThread, 
									SIGNAL(started()), 
									SLOT(processDeviceData()));*/
			break;
	}
}

void DataEngine::startAudioOutProcessor(QThread::Priority prio) {

	Q_UNUSED (prio)
}

void DataEngine::stopAudioOutProcessor() {
}

//********************************************************
// create, start/stop winde band data processor

void DataEngine::createWideBandDataProcessor() {

	int size;

	if (io.mercuryFW > 32 || io.hermesFW > 11)
		size = BIGWIDEBANDSIZE;
	else
		size = SMALLWIDEBANDSIZE;
	
	m_wbDataProcessor = new WideBandDataProcessor(&io, m_serverMode, size);

	m_wbDataProcThread = new QThreadEx();
	m_wbDataProcessor->moveToThread(m_wbDataProcThread);
	m_wbDataProcessor->connect(
							m_wbDataProcThread, 
							SIGNAL(started()), 
							SLOT(processWideBandData()));

	CHECKED_CONNECT(
		set, 
		SIGNAL(spectrumAveragingChanged(QObject*, int, bool)), 
		m_wbDataProcessor, 
		SLOT(setWbSpectrumAveraging(QObject*, int, bool)));

	CHECKED_CONNECT(
		m_wbDataProcessor,
		SIGNAL(wbSpectrumBufferChanged(const qVectorFloat&)),
		set,
		SLOT(setWidebandSpectrumBuffer(const qVectorFloat&)));
}

bool DataEngine::startWideBandDataProcessor(QThread::Priority prio) {

	m_wbDataProcThread->start(prio);//(QThread::TimeCriticalPriority);//(QThread::HighPriority);//(QThread::LowPriority);

	if (m_wbDataProcThread->isRunning()) {
					
		m_wbDataRcvrThreadRunning = true;
		io.networkIOMutex.lock();
		DATA_ENGINE_DEBUG << "wide band data processor thread started.";
		io.networkIOMutex.unlock();

		return true;
	}
	else {

		m_wbDataRcvrThreadRunning = false;
		setSystemState(QSDR::WideBandDataProcessThreadError, m_hwInterface, m_serverMode, QSDR::DataEngineDown);
		return false;
	}
}

void DataEngine::stopWideBandDataProcessor() {

	if (m_wbDataProcThread->isRunning()) {
					
		m_wbDataProcessor->stop();
		if (io.wb_queue.isEmpty())
			io.wb_queue.enqueue(m_datagram);

		m_wbDataProcThread->quit();
		m_wbDataProcThread->wait();
		delete m_wbDataProcThread;
		delete m_wbDataProcessor;
		m_wbDataProcessor = 0;

		m_wbDataRcvrThreadRunning = false;
		
		DATA_ENGINE_DEBUG << "wide band data processor thread deleted.";
	}
	else
		DATA_ENGINE_DEBUG << "wide band data processor thread wasn't started.";
}
 
//********************************************************
// create, start/stop chirp processor
void DataEngine::createChirpDataProcessor() {

	m_chirpProcessor = new ChirpProcessor(&io);
	DATA_ENGINE_DEBUG << "chirp decoder initialized";
	
	CHECKED_CONNECT_OPT(
		m_audioEngine, 
		SIGNAL(chirpSignalChanged()),
		m_chirpProcessor,
		SLOT(generateLocalChirp()),
		Qt::DirectConnection);

	m_audioEngine->reset();
	if (m_audioEngine->generateSweptTone())
		DATA_ENGINE_DEBUG << "audio chirp signal initialized";
	else
		DATA_ENGINE_DEBUG << "audio chirp signal initialization failed";


	m_chirpDataProcThread = new QThreadEx();
	m_chirpProcessor->moveToThread(m_chirpDataProcThread);
	m_chirpProcessor->connect(
						m_chirpDataProcThread, 
						SIGNAL(started()),
						m_chirpProcessor,
						SLOT(processChirpData()));

	m_chirpInititalized = true;
}

bool DataEngine::startChirpDataProcessor(QThread::Priority prio) {

	m_chirpDataProcThread->start(prio);//(QThread::TimeCriticalPriority);//(QThread::HighPriority);//(QThread::LowPriority);
				
	if (m_chirpDataProcThread->isRunning()) {
					
		m_chirpDataProcThreadRunning = true;
		io.networkIOMutex.lock();
		DATA_ENGINE_DEBUG << "chirp data processor thread started.";
		io.networkIOMutex.unlock();

		return true;
	}
	else {

		m_chirpDataProcThreadRunning = false;
		setSystemState(QSDR::DataProcessThreadError, m_hwInterface, m_serverMode, QSDR::DataEngineDown);
		return false;
	}
}

void DataEngine::stopChirpDataProcessor() {

	if (m_chirpInititalized) {

		m_chirpProcessor->stop();
		if (io.chirp_queue.isEmpty()) {
				
			QList<qreal> buf;
			for (int i = 0; i < 128; i++) buf << 0.0f;
				io.chirp_queue.enqueue(buf);
			}

			m_chirpDataProcThread->quit();
			m_chirpDataProcThread->wait();
			delete m_chirpDataProcThread;
			delete m_chirpProcessor;
			m_chirpProcessor = 0;

			if (m_hwInterface == QSDR::NoInterfaceMode) {

				//freeCPX(io.cpxIn);
				//freeCPX(io.cpxOut);
				delete m_chirpDspEngine;

				while (!io.chirp_queue.isEmpty())
					io.chirp_queue.dequeue();

				DATA_ENGINE_DEBUG << "io.cpxIn, io.cpxOut, fft deleted, io.chirp_queue empty.";
			}

			m_chirpInititalized = false;

			DATA_ENGINE_DEBUG << "chirp data processor thread deleted.";
	}
	else
		DATA_ENGINE_DEBUG << "chirp data processor thread wasn't started.";
}
 
//********************************************************
// create, start/stop audio receiver

void DataEngine::createAudioReceiver() {

	m_audioReceiver = new AudioReceiver(&io);

	CHECKED_CONNECT(
		m_audioReceiver, 
		SIGNAL(rcveIQEvent(QObject *, int)), 
		this, 
		SLOT(setRcveIQSignal(QObject *, int)));

	CHECKED_CONNECT(
		m_audioReceiver, 
		SIGNAL(clientConnectedEvent(bool)), 
		this, 
		SLOT(setClientConnected(bool)));

	
	m_AudioRcvrThread = new QThreadEx();
	m_audioReceiver->moveToThread(m_AudioRcvrThread);

	m_audioReceiver->connect(
						m_AudioRcvrThread, 
						SIGNAL(started()), 
						SLOT(initClient()));
}
 

void DataEngine::processFileBuffer(const QList<qreal> buffer) {


	int topsize = 2*BUFFER_SIZE - 1;
	//float specMax = -100.0f;
	//float specMin = 0.0f;

	Q_ASSERT(buffer.length() == 128);

	for (int i = 0; i < 64; i++) {

		cpxIn[i + m_rxSamples].re = buffer.at(2*i);
		cpxIn[i + m_rxSamples].im = buffer.at(2*i+1);

		chirpData << buffer.at(2*i);
		chirpData << buffer.at(2*i+1);

		m_chirpSamples++;
		if (m_chirpSamples == io.samplerate) {

			io.chirp_queue.enqueue(chirpData);
			chirpData.clear();
			m_chirpSamples = 0;
		}
	}
	m_rxSamples += 64;

	if (m_rxSamples == 2*BUFFER_SIZE) {

		m_chirpDspEngine->fft->DoFFTWForward(cpxIn, cpxOut, 2*BUFFER_SIZE);

		// reorder the spectrum buffer
		for (int i = 0; i < BUFFER_SIZE; i++) {

			m_spectrumBuffer[topsize - i] =
				(float)(10.0 * log10(MagCPX(cpxOut[i+BUFFER_SIZE]) + 1.5E-45));
			m_spectrumBuffer[BUFFER_SIZE - i] =
				(float)(10.0 * log10(MagCPX(cpxOut[i]) + 1.5E-45));
		}

		/*float specMean = 0.0f;
		for (int i = BUFFER_SIZE+20; i < BUFFER_SIZE+105; i++) {

			specMean += m_spectrumBuffer[i];
			if (m_spectrumBuffer[i] > specMax) specMax = m_spectrumBuffer[i];
			if (m_spectrumBuffer[i] < specMin) specMin = m_spectrumBuffer[i];
		}*/
		//specMean *= 1.0f/BUFFER_SIZE;
		//DATA_PROCESSOR_DEBUG << "pan min" << specMin << "max" << specMax << "mean" << specMean;

		SleeperThread::usleep(42667);

		//emit spectrumBufferChanged(m_spectrumBuffer);
		//set->setSpectrumBuffer(m_spectrumBuffer);
		//set->setSpectrumBuffer(0, m_spectrumBuffer);

		m_rxSamples = 0;
	}
}


//*****************************************************************************
//

void DataEngine::systemStateChanged(
	QObject *sender, 
	QSDR::_Error err, 
	QSDR::_HWInterfaceMode hwmode, 
	QSDR::_ServerMode mode, 
	QSDR::_DataEngineState state)
{
	Q_UNUSED (sender)
	Q_UNUSED (err)

	io.mutex.lock();
	if (m_hwInterface != hwmode)
		m_hwInterface = hwmode;
		
	if (m_serverMode != mode)
		m_serverMode = mode;
		
	if (m_dataEngineState != state)
		m_dataEngineState = state;

	io.mutex.unlock();
}

void DataEngine::setSystemState(
		QSDR::_Error err,
		QSDR::_HWInterfaceMode hwmode,
		QSDR::_ServerMode statemode,
		QSDR::_DataEngineState enginestate)
{
	io.networkIOMutex.lock();
	set->setSystemState(this, err, hwmode, statemode, enginestate);
	io.networkIOMutex.unlock();
}

float DataEngine::getFilterSizeCalibrationOffset() {

    //int size=1024; // dspBufferSize
    float i = log10((qreal) BUFFER_SIZE);
    return 3.0f*(11.0f - i);
}

void DataEngine::searchHpsdrNetworkDevices() {

	if (!m_discoverer) createDiscoverer();

	// HPSDR network IO thread
	if (!startDiscoverer(QThread::NormalPriority)) {

		DATA_ENGINE_DEBUG << "HPSDR network discovery thread could not be started.";
		return;
	}

	io.networkIOMutex.lock();
	io.devicefound.wait(&io.networkIOMutex);

	//m_discoverer->findHPSDRDevices();

	// stop the discovery thread
	io.networkIOMutex.unlock();
	stopDiscoverer();
}

void DataEngine::setHPSDRDeviceNumber(int value) {

	m_hpsdrDevices = value;
}

void DataEngine::rxListChanged(QList<Receiver *> list) {

	io.mutex.lock();
	RX = list;
	io.mutex.unlock();
}

void DataEngine::setCurrentReceiver(QObject *sender, int rx) {

	Q_UNUSED(sender)

	io.mutex.lock();
	io.currentReceiver = rx;
	io.mutex.unlock();
}

void DataEngine::setFramesPerSecond(QObject *sender, int rx, int value) {

	Q_UNUSED(sender)
	Q_UNUSED(rx)
	Q_UNUSED(value)

	/*io.mutex.lock();
	if (m_fpsList.length() > 0)
		m_fpsList[rx] = (int)(1000000.0/value);
	io.mutex.unlock();*/
}

void DataEngine::setSampleRate(QObject *sender, int value) {

	Q_UNUSED(sender)

	io.mutex.lock();
	switch (value) {
	
		case 48000:
			io.samplerate = value;
			io.speed = 0;
			io.outputMultiplier = 1;
			break;
			
		case 96000:
			io.samplerate = value;
			io.speed = 1;
			io.outputMultiplier = 2;
			break;
			
		case 192000:
			io.samplerate = value;
			io.speed = 2;
			io.outputMultiplier = 4;
			break;
			
		case 384000:
			io.samplerate = value;
			io.speed = 3;
			io.outputMultiplier = 8;
			break;

		default:
			DATA_ENGINE_DEBUG << "invalid sample rate !\n";
			stop();
			break;
	}

	io.mutex.unlock();

	emit outMultiplierEvent(io.outputMultiplier);
}

void DataEngine::setMercuryAttenuator(QObject *sender, HamBand band, int value) {

	Q_UNUSED(sender)
	Q_UNUSED(band)

	io.mutex.lock();
	io.ccTx.mercuryAttenuator = value;
	io.mutex.unlock();
}

void DataEngine::setMercuryAttenuators(QObject *sender, QList<int> attn) {

	Q_UNUSED(sender)

	io.mutex.lock();
	io.ccTx.mercuryAttenuators = attn;
	io.mutex.unlock();
}

void DataEngine::setDither(QObject *sender, int value) {

	Q_UNUSED(sender)

	io.mutex.lock();
	io.ccTx.dither = value;
	io.mutex.unlock();
}

void DataEngine::setRandom(QObject *sender, int value) {

	Q_UNUSED(sender)

	io.mutex.lock();
	io.ccTx.random = value;
	io.mutex.unlock();
}

void DataEngine::set10MhzSource(QObject *sender, int source) {

	Q_UNUSED(sender)

	io.mutex.lock();
	io.control_out[1] = io.control_out[1] & 0xF3;
	io.control_out[1] = io.control_out[1] | (source << 2);
	io.mutex.unlock();
}

void DataEngine::set122_88MhzSource(QObject *sender, int source) {

	Q_UNUSED(sender)

	io.mutex.lock();
	io.control_out[1] = io.control_out[1] & 0xEF;
	io.control_out[1] = io.control_out[1] | (source << 4);
	io.mutex.unlock();
}

void DataEngine::setMicSource(QObject *sender, int source) {

	Q_UNUSED(sender)

	io.mutex.lock();
	io.control_out[1] = io.control_out[1] & 0x7F;
	io.control_out[1] = io.control_out[1] | (source << 7);
	io.mutex.unlock();
}

void DataEngine::setMercuryClass(QObject *sender, int value) {

	Q_UNUSED(sender)

	io.mutex.lock();
	io.rxClass = value;
	io.mutex.unlock();
}

void DataEngine::setMercuryTiming(QObject *sender, int value) {

	Q_UNUSED(sender)

	io.mutex.lock();
	io.timing = value;
	io.mutex.unlock();
}

void DataEngine::setAlexConfiguration(quint16 conf) {

	io.mutex.lock();
	io.ccTx.alexConfig = conf;
	DATA_ENGINE_DEBUG << "Alex Configuration = " << io.ccTx.alexConfig;
	io.mutex.unlock();
}

void DataEngine::setAlexStates(HamBand band, const QList<int> &states) {

	Q_UNUSED (band)

	io.mutex.lock();
	io.ccTx.alexStates = states;
	DATA_ENGINE_DEBUG << "Alex States = " << io.ccTx.alexStates;
	io.mutex.unlock();
}

void DataEngine::setPennyOCEnabled(bool value) {

	io.mutex.lock();
	io.ccTx.pennyOCenabled = value;
	io.mutex.unlock();
}

void DataEngine::setRxJ6Pins(const QList<int> &list) {

	io.mutex.lock();
	io.ccTx.rxJ6pinList = list;
	io.mutex.unlock();

}

void DataEngine::setTxJ6Pins(const QList<int> &list) {

	io.mutex.lock();
	io.ccTx.txJ6pinList = list;
	io.mutex.unlock();
}

void DataEngine::setRcveIQSignal(QObject *sender, int value) {

	emit rcveIQEvent(sender, value);
}

void DataEngine::setPenelopeVersion(QObject *sender, int version) {

	emit penelopeVersionInfoEvent(sender, version);
}

void DataEngine::setHwIOVersion(QObject *sender, int version) {

	emit hwIOVersionInfoEvent(sender, version);
}

void DataEngine::setNumberOfRx(QObject *sender, int value) {

	Q_UNUSED(sender)

	if (io.receivers == value) return;
	
	io.mutex.lock();
	io.receivers = value;
	io.mutex.unlock();
	//io.control_out[4] &= 0xc7;
	//io.control_out[4] |= (value - 1) << 3;

	DATA_ENGINE_DEBUG << "number of receivers set to " << QString::number(value);
}

void DataEngine::setTimeStamp(QObject *sender, bool value) {

	Q_UNUSED(sender)

	if (io.timeStamp == value) return;

	io.mutex.lock();
	io.timeStamp = value;
	io.mutex.unlock();
	//io.control_out[4] &= 0xc7;
	//RRK io.control_out[4] |= value << 6;

	if (value)
		DATA_ENGINE_DEBUG << "set time stamp on";
	else
		DATA_ENGINE_DEBUG << "set time stamp off";
}

void DataEngine::setRxSocketState(int rx, const char* prop, QString str) {

	RX[rx]->setProperty(prop, str);
	set->setRxList(RX);
}

void DataEngine::setRxPeerAddress(int rx, QHostAddress address) {

	RX[rx]->setPeerAddress(address);
	set->setRxList(RX);
}

void DataEngine::setRx(int rx) {

	io.mutex.lock();
	RX[rx]->setReceiver(rx);
	set->setRxList(RX);
	io.mutex.unlock();
}

void DataEngine::setRxClient(int rx, int client) {

	io.mutex.lock();
	RX[rx]->setClient(client);
	set->setRxList(RX);
	io.mutex.unlock();
}

void DataEngine::setClientConnected(QObject* sender, int rx) {

	Q_UNUSED(sender)

	if (!io.clientList.contains(rx)) {

		io.clientList.append(rx);
		io.audio_rx = rx;

		m_AudioRcvrThread->quit();
		m_AudioRcvrThread->wait();
		m_AudioRcvrThread->start();
	}
	else {

		io.sendIQ_toggle = true;
		io.rcveIQ_toggle = false;
		m_AudioRcvrThread->start();
	}
}

void DataEngine::setClientConnected(bool value) {

	m_clientConnected = value;
}

void DataEngine::setClientDisconnected(int client) {

	Q_UNUSED(client)
	/*if (m_clientConnected) {

		m_AudioRcvrThread->quit();
		m_AudioRcvrThread->wait();
		if (!m_AudioRcvrThread->isRunning())
			DATA_ENGINE_DEBUG << "audio receiver thread stopped.";

		m_clientConnected = false;		
	}
	sync_toggle = true;
	adc_toggle = false;*/
}

//void DataEngine::setAudioInProcessorRunning(bool value) {
//
//	//m_audioInProcessorRunning = value;
//}

void DataEngine::setAudioReceiver(QObject *sender, int rx) {

	Q_UNUSED(sender)

	io.mutex.lock();
	emit audioRxEvent(rx);
	io.mutex.unlock();
}

void DataEngine::setIQPort(int rx, int port) {

	io.mutex.lock();
	RX[rx]->setIQPort(port);
	set->setRxList(RX);
	io.mutex.unlock();
}

void DataEngine::setRxConnectedStatus(QObject* sender, int rx, bool value) {

	Q_UNUSED(sender)

	io.mutex.lock();
	RX[rx]->setConnectedStatus(value);
	set->setRxList(RX);
	io.mutex.unlock();
}

void DataEngine::setHamBand(QObject *sender, int rx, bool byBtn, HamBand band) {

	Q_UNUSED(sender)
	Q_UNUSED(rx)
	Q_UNUSED(byBtn)

	io.mutex.lock();
	io.ccTx.currentBand = band;
	io.mutex.unlock();
}

void DataEngine::setFrequency(QObject* sender, int mode, int rx, long frequency) {

	Q_UNUSED (sender)
	Q_UNUSED (mode)

	//RX[rx]->setFrequency(frequency);
	RX[rx]->setCtrFrequency(frequency);
	io.rx_freq_change = rx;
	io.tx_freq_change = rx;
}

void DataEngine::loadWavFile(const QString &fileName) {

	if (m_audioEngine->loadFile(fileName))
		m_soundFileLoaded = true;
	else
		m_soundFileLoaded = false;
}

void DataEngine::suspend() {

	m_audioEngine->suspend();
}

void DataEngine::startPlayback() {

	m_audioEngine->startPlayback();
}

void DataEngine::showSettingsDialog() {

	m_audioEngine->showSettingsDialog();
}

void DataEngine::setAudioFileFormat(QObject *sender, const QAudioFormat &format) {

	Q_UNUSED (sender)
	Q_UNUSED (format)
}

void DataEngine::setAudioFilePosition(QObject *sender, qint64 position) {

	Q_UNUSED (sender)
	Q_UNUSED (position)
}

void DataEngine::setAudioFileBuffer(QObject *sender, qint64 position, qint64 length, const QByteArray &buffer) {

	Q_UNUSED (sender)

    m_audioFileBufferPosition = position;
    m_audioFileBufferLength = length;
	m_audioFileBuffer = buffer;

	//DATA_ENGINE_DEBUG << "audio file length" << m_audioFileBufferLength;
}

void DataEngine::setAudioFileBuffer(const QList<qreal> &buffer) {

	io.inputBuffer = buffer;
	
	/*for (int i = 0; i < buffer.length(); i++) {

		DATA_ENGINE_DEBUG << "i" << i << "audioBuffer" << io.inputBuffer.at(i);
	}*/
}
 
// *********************************************************************
// Data processor

DataProcessor::DataProcessor(
					DataEngine *de, 
					QSDR::_ServerMode serverMode,
					QSDR::_HWInterfaceMode hwMode)
	: QObject()
	, de(de)
	, set(Settings::instance())
	, m_dataProcessorSocket(0)
	, m_serverMode(serverMode)
	, m_hwInterface(hwMode)
	, m_socketConnected(false)
	, m_setNetworkDeviceHeader(true)
	, m_chirpGateBit(true)
	, m_chirpBit(false)
	, m_chirpStart(false)
	, m_bytes(0)
	, m_offset(0)
	, m_length(0)
	, m_rxSamples(0)
	, m_chirpSamples(0)
	, m_chirpStartSample(0)
	, m_idx(IO_HEADER_SIZE)
	, m_sendState(0)
	, m_stopped(false)
{
	m_IQSequence = 0L;
	m_sequenceHi = 0L;
	
	m_IQDatagram.resize(0);

	m_SyncChangedTime.start();
	m_ADCChangedTime.start();

	m_fwCount = 0;

	m_sendSequence = 0L;
	m_oldSendSequence = 0L;

	m_deviceSendDataSignature.resize(4);
	m_deviceSendDataSignature[0] = (char)0xEF;
	m_deviceSendDataSignature[1] = (char)0xFE;
	m_deviceSendDataSignature[2] = (char)0x01;
	m_deviceSendDataSignature[3] = (char)0x02;

	//socket = new QUdpSocket();
	m_deviceAddress = set->getCurrentMetisCard().ip_address;
}

DataProcessor::~DataProcessor() {
}

void DataProcessor::stop() {

	m_stopped = true;
}

void DataProcessor::initDataProcessorSocket() {

	m_dataProcessorSocket = new QUdpSocket();

	/*m_dataProcessorSocket->bind(QHostAddress(set->getHPSDRDeviceLocalAddr()),
								  23000, 
								  QUdpSocket::ReuseAddressHint | QUdpSocket::ShareAddress);

	int newBufferSize = 64 * 1024;

	if (::setsockopt(m_dataProcessorSocket->socketDescriptor(), SOL_SOCKET,
                     SO_RCVBUF, (char *)&newBufferSize, sizeof(newBufferSize)) == -1) {
						 
						 DATA_ENGINE_DEBUG << "initDataProcessorSocket error setting m_dataProcessorSocket buffer size.";
	}*/

	//m_dataProcessorSocket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
	//m_dataProcessorSocket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);

	CHECKED_CONNECT(
		m_dataProcessorSocket, 
		SIGNAL(error(QAbstractSocket::SocketError)), 
		this, 
		SLOT(displayDataProcessorSocketError(QAbstractSocket::SocketError)));
}

void DataProcessor::displayDataProcessorSocketError(QAbstractSocket::SocketError error) {

	DATA_PROCESSOR_DEBUG << "data processor socket error: " << error;
}

void DataProcessor::processDeviceData() {

	//if (m_serverMode == QSDR::ExternalDSP)
	//	initDataProcessorSocket();

	DATA_PROCESSOR_DEBUG << "Data Processor thread: " << this->thread();
	forever {

		//m_dataEngine->processInputBuffer(m_dataEngine->io.iq_queue.dequeue());
		QByteArray buf = de->io.iq_queue.dequeue();
		//de->processInputBuffer(buf.left(BUFFER_SIZE/2));
		//de->processInputBuffer(buf.right(BUFFER_SIZE/2));

		processInputBuffer(buf.left(BUFFER_SIZE/2));
		processInputBuffer(buf.right(BUFFER_SIZE/2));

		if (de->io.iq_queue.isFull()) { 
			DATA_PROCESSOR_DEBUG << "IQ queue full!";
		}
		
		QMutexLocker locker(&m_mutex);
		if (m_stopped) {
			m_stopped = false;
			break;
		}
	}

//	if (m_serverMode == QSDR::ExternalDSP) {
//
//		disconnect(this);
//		m_dataProcessorSocket->close();
//		delete m_dataProcessorSocket;
//		m_dataProcessorSocket = NULL;
//
//		m_socketConnected = false;
//	}
}

void DataProcessor::processData() {

	forever {

		de->processFileBuffer(de->io.data_queue.dequeue());

		m_mutex.lock();
		if (m_stopped) {
			m_stopped = false;
			m_mutex.unlock();
			break;
		}
		m_mutex.unlock();
	}
}

void DataProcessor::externalDspProcessing(int rx) {

	// keep UDP packets < 512 bytes 
	// 8 bytes sequency number, 2 bytes offset, 2 bytes length, 500 bytes data

	if (!m_socketConnected) {

		m_dataProcessorSocket->connectToHost(de->RX[rx]->getPeerAddress(), de->RX[rx]->getIQPort());

#if defined(Q_OS_WIN32)
		//int newBufferSize = 64 * 1024;
		int newBufferSize = 16 * 1024;

		if (::setsockopt(m_dataProcessorSocket->socketDescriptor(), SOL_SOCKET,
                     SO_RCVBUF, (char *)&newBufferSize, sizeof(newBufferSize)) == -1) {
				 
		  DATA_PROCESSOR_DEBUG << "externalDspProcessing error setting m_dataProcessorSocket buffer size.";
		}
#endif

		m_socketConnected = true;
	}
	
#ifndef __linux__
	m_sequenceHi = 0L;
#endif
	
	/*QUdpSocket socket;
	CHECKED_CONNECT(&socket, 
			SIGNAL(error(QAbstractSocket::SocketError)), 
			this, 
			SLOT(displayDataProcessorSocketError(QAbstractSocket::SocketError)));*/

	m_offset = 0;
	//m_IQDatagram.append(reinterpret_cast<const char*>(&m_dataEngine->rxList[rx]->input_buffer), sizeof(m_dataEngine->rxList[rx]->input_buffer));
	m_IQDatagram.append(reinterpret_cast<const char*>(&de->RX[rx]->inBuf), sizeof(de->RX[rx]->inBuf));

	m_IQDatagram.append(reinterpret_cast<const char*>(&de->RX[rx]->inBuf), sizeof(de->RX[rx]->inBuf));
		
	while (m_offset < m_IQDatagram.size()) {
	
		m_length = m_IQDatagram.size() - m_offset;
		
		if (m_length > 500)  
			m_length = 500;

		QByteArray datagram;
		datagram += QByteArray(reinterpret_cast<const char*>(&m_IQSequence), sizeof(m_IQSequence));
		datagram += QByteArray(reinterpret_cast<const char*>(&m_sequenceHi), sizeof(m_sequenceHi));
		datagram += QByteArray(reinterpret_cast<const char*>(&m_offset), sizeof(m_offset));
		datagram += QByteArray(reinterpret_cast<const char*>(&m_length), sizeof(m_length));
		datagram += m_IQDatagram.mid(m_offset, m_length);
		
		if (m_dataProcessorSocket->write(datagram) < 0)
		/*if (m_dataProcessorSocket->writeDatagram(datagram,
											m_dataEngine->rxList[rx]->getPeerAddress(),
											m_dataEngine->rxList[rx]->getIQPort()) < 0)*/
		//if (socket.writeDatagram(datagram,
		//						 m_dataEngine->rxList[rx]->getPeerAddress(),
		//						 m_dataEngine->rxList[rx]->getIQPort()) < 0)
		{
			if (!de->io.sendIQ_toggle) {  // toggles the sendIQ signal

				de->set->setSendIQ(2);
				de->io.sendIQ_toggle = true;
			}

			DATA_ENGINE_DEBUG	<< "externalDspProcessing error sending data to client:" 
								<< m_dataProcessorSocket->errorString();
		}
		else {
		
			//socket.flush();
			if (de->io.sendIQ_toggle) { // toggles the sendIQ signal
				
				de->set->setSendIQ(1);
				de->io.sendIQ_toggle = false;
			}
		}
		m_offset += m_length;
	}
	m_IQDatagram.resize(0);
	m_IQSequence++;
}

void DataProcessor::externalDspProcessingBig(int rx) {
	
	m_IQDatagram.append(reinterpret_cast<const char*>(&de->RX[rx]->in), sizeof(de->RX[rx]->in));
		
	if (m_dataProcessorSocket->writeDatagram(m_IQDatagram.data(), 
										m_IQDatagram.size(), 
										de->RX[rx]->getPeerAddress(),
										de->RX[rx]->getIQPort()) < 0)
										
	{		
		if (!de->io.sendIQ_toggle) {  // toggles the sendIQ signal

			de->set->setSendIQ(2);
			de->io.sendIQ_toggle = true;
		}

		DATA_PROCESSOR_DEBUG << "error sending data to client:" << m_dataProcessorSocket->errorString();
	}
	else {
	
		m_dataProcessorSocket->flush();
		if (de->io.sendIQ_toggle) { // toggles the sendIQ signal
				
			de->set->setSendIQ(1);
			de->io.sendIQ_toggle = false;
		}
	}
	m_IQDatagram.resize(0);
}

void DataProcessor::processInputBuffer(const QByteArray &buffer) {
	
	//DATA_PROCESSOR_DEBUG << "processInputBuffer: " << this->thread();
	int s = 0;

	if (buffer.at(s++) == SYNC && buffer.at(s++) == SYNC && buffer.at(s++) == SYNC)
	{
		// extract C&C bytes
        decodeCCBytes(buffer.mid(3, 5));
        s += 5;

        switch (de->io.receivers)
		{
            case 1: m_maxSamples = 512-0;  break;
            case 2: m_maxSamples = 512-0;  break;
            case 3: m_maxSamples = 512-4;  break;
            case 4: m_maxSamples = 512-10; break;
            case 5: m_maxSamples = 512-24; break;
            case 6: m_maxSamples = 512-10; break;
            case 7: m_maxSamples = 512-20; break;
            case 8: m_maxSamples = 512-4;  break;
            case 9: m_maxSamples = 512-0;  break;
            case 10: m_maxSamples = 512-8;  break;
            case 11: m_maxSamples = 512-28;  break;
            case 12: m_maxSamples = 512-60;  break;
            case 13: m_maxSamples = 512-24;  break;
            case 14: m_maxSamples = 512-74;  break;
            case 15: m_maxSamples = 512-44;  break;
            case 16: m_maxSamples = 512-14;  break;
            case 17: m_maxSamples = 512-88;  break;
            case 18: m_maxSamples = 512-64;  break;
            case 19: m_maxSamples = 512-40;  break;
            case 20: m_maxSamples = 512-16;  break;
        }

        // extract the samples
        while (s < m_maxSamples)
		{
            // extract each of the receivers
            for (int r = 0; r < de->io.receivers; r++)
			{
                m_leftSample   = (int)((  signed char) buffer.at(s++)) << 16;
                m_leftSample  += (int)((unsigned char) buffer.at(s++)) << 8;
                m_leftSample  += (int)((unsigned char) buffer.at(s++));
                m_rightSample  = (int)((  signed char) buffer.at(s++)) << 16;
                m_rightSample += (int)((unsigned char) buffer.at(s++)) << 8;
                m_rightSample += (int)((unsigned char) buffer.at(s++));

				m_lsample = (float)(m_leftSample / 8388607.0f);
				m_rsample = (float)(m_rightSample / 8388607.0f);

				/*if (m_serverMode == QSDR::ChirpWSPR &&
					m_chirpInititalized &&
					m_chirpSamples < io.samplerate)
				{
					chirpData << m_lsample;
					chirpData << m_rsample;
				}*/

				if (de->RX.at(r)->qtdsp) {

					de->RX[r]->inBuf[m_rxSamples].re = m_lsample; // 24 bit sample
					de->RX[r]->inBuf[m_rxSamples].im = m_rsample; // 24 bit sample
				}
            }

            m_micSample = (int)((signed char) buffer.at(s++)) << 8;

			// extract chirp signal time stamp
			//m_chirpBit = (buffer.at(s) & 0x01);// == 0x01;

			m_micSample += (int)((unsigned char) buffer.at(s++));
    		m_micSample_float = (float) m_micSample / 32767.0f * de->io.mic_gain; // 16 bit sample

            // add to buffer
            de->io.mic_left_buffer[m_rxSamples]  = m_micSample_float;
            de->io.mic_right_buffer[m_rxSamples] = 0.0f;

			////m_chirpSamples++;

			//if (m_serverMode == QSDR::ChirpWSPR && m_chirpInititalized)
			//{
			//	if (m_chirpBit)
			//	{
			//		if (m_chirpGateBit)
			//		{
			//			// we've found the rising edge of the GPS 1PPS signal, so we set the samples
			//			// counter back to zero in order to have a simple and precise synchronisation
			//			// with the local chirp.
			//			io.networkIOMutex.lock();
			//			DATA_ENGINE_DEBUG << "GPS 1 PPS";
			//			io.networkIOMutex.unlock();

			//			// remove the last sample (real and imag) and enqueue the buffer
			//			chirpData.removeLast();
			//			chirpData.removeLast();
			//			io.chirp_queue.enqueue(chirpData);

			//			// empty the buffer and add the last sample, which is the starting point of the chirp
			//			m_chirpSamples = 0;
			//			chirpData.clear();

			//			chirpData << m_lsample;
			//			chirpData << m_rsample;

			//			m_chirpStart = true;
			//			m_chirpStartSample = m_rxSamples;
			//			m_chirpGateBit = false;
			//		}
			//	}
			//	else
			//		m_chirpGateBit = true;
			//}
			m_rxSamples++;
			m_chirpSamples++;

			// when we have enough rx samples we start the DSP processing.
            if (m_rxSamples == BUFFER_SIZE) {

				for (int r = 0; r < de->io.receivers; r++) {

					if (de->RX.at(r)->qtdsp) {
						
						QMetaObject::invokeMethod(de->RX.at(r), "dspProcessing", Qt::DirectConnection);// Qt::QueuedConnection);
					}
				}
				m_rxSamples = 0;
            }
        }
    }
	else {

		if (m_SyncChangedTime.elapsed() > 10) {

			set->setProtocolSync(2);
			m_SyncChangedTime.restart();
		}
	}
}

void DataProcessor::decodeCCBytes(const QByteArray &buffer) {

	de->io.ccRx.ptt    = (bool)((buffer.at(0) & 0x01) == 0x01);
	de->io.ccRx.dash   = (bool)((buffer.at(0) & 0x02) == 0x02);
	de->io.ccRx.dot    = (bool)((buffer.at(0) & 0x04) == 0x04);
	de->io.ccRx.lt2208 = (bool)((buffer.at(1) & 0x01) == 0x01);

	de->io.ccRx.roundRobin = (uchar)(buffer.at(0) >> 3);
	
    switch (de->io.ccRx.roundRobin) // cycle through C0
	{
		case 0:

			if (de->io.ccRx.lt2208) // check ADC signal
			{
				if (m_ADCChangedTime.elapsed() > 50)
				{
					set->setADCOverflow(2);
					m_ADCChangedTime.restart();
				}
			}

			//qDebug() << "CC: " << io.ccRx.roundRobin;
			if (m_hwInterface == QSDR::Hermes)
			{
				de->io.ccRx.hermesI01 = (bool)((buffer.at(1) & 0x02) == 0x02);
				de->io.ccRx.hermesI02 = (bool)((buffer.at(1) & 0x04) == 0x04);
				de->io.ccRx.hermesI03 = (bool)((buffer.at(1) & 0x08) == 0x08);
				de->io.ccRx.hermesI04 = (bool)((buffer.at(1) & 0x10) == 0x10);
				//qDebug()	<< "Hermes IO 1: " << io.ccRx.hermesI01 
				//			<< "2: " << io.ccRx.hermesI02 
				//			<< "3: " << io.ccRx.hermesI03 
				//			<< "4: " << io.ccRx.hermesI04;
			}

			if (m_fwCount < 100)
			{
				if (m_hwInterface == QSDR::Metis)
				{
					if (de->io.ccRx.devices.mercuryFWVersion != buffer.at(2))
					{
						de->io.ccRx.devices.mercuryFWVersion = buffer.at(2);
						set->setMercuryVersion(de->io.ccRx.devices.mercuryFWVersion);
						de->io.networkIOMutex.lock();
						DATA_PROCESSOR_DEBUG << "Mercury firmware version: " << qPrintable(QString::number(buffer.at(2)));
						de->io.networkIOMutex.unlock();
					}

					if (de->io.ccRx.devices.penelopeFWVersion != buffer.at(3))
					{
						de->io.ccRx.devices.penelopeFWVersion = buffer.at(3);
						de->io.ccRx.devices.pennylaneFWVersion = buffer.at(3);
						set->setPenelopeVersion(de->io.ccRx.devices.penelopeFWVersion);
						set->setPennyLaneVersion(de->io.ccRx.devices.penelopeFWVersion);
						de->io.networkIOMutex.lock();
						DATA_PROCESSOR_DEBUG << "Penelope/Pennylane firmware version: " << qPrintable(QString::number(buffer.at(3)));
						de->io.networkIOMutex.unlock();
					}

					if (de->io.ccRx.devices.metisFWVersion != buffer.at(4))
					{
						de->io.ccRx.devices.metisFWVersion = buffer.at(4);
						set->setMetisVersion(de->io.ccRx.devices.metisFWVersion);
						de->io.networkIOMutex.lock();
						DATA_PROCESSOR_DEBUG << "Metis firmware version: " << qPrintable(QString::number(buffer.at(4)));
						de->io.networkIOMutex.unlock();
					}
				}
				else if (set->getHWInterface() == QSDR::Hermes) {

					if (de->io.ccRx.devices.hermesFWVersion != buffer.at(4)) {

						de->io.ccRx.devices.hermesFWVersion = buffer.at(4);
						set->setHermesVersion(de->io.ccRx.devices.hermesFWVersion);
						de->io.networkIOMutex.lock();
						DATA_ENGINE_DEBUG << "Hermes firmware version: " << qPrintable(QString::number(buffer.at(4)));
						de->io.networkIOMutex.unlock();
					}
				}
				m_fwCount++;
			}
			break;

		case 1:

			//qDebug() << "CC: " << io.ccRx.roundRobin;
			// forward power
			if (set->getPenelopePresence() || (m_hwInterface == QSDR::Hermes)) { // || set->getPennyLanePresence()

				de->io.ccRx.ain5 = (quint16)((quint16)(buffer.at(1) << 8) + (quint16)buffer.at(2));

				de->io.penelopeForwardVolts = (qreal)(3.3 * (qreal)de->io.ccRx.ain5 / 4095.0);
				de->io.penelopeForwardPower = (qreal)(de->io.penelopeForwardVolts * de->io.penelopeForwardVolts / 0.09);
			}
			//qDebug() << "penelopeForwardVolts: " << io.penelopeForwardVolts << "penelopeForwardPower" << io.penelopeForwardPower;

			if (set->getAlexPresence()) { //|| set->getApolloPresence()) {

				de->io.ccRx.ain1 = (quint16)((quint16)(buffer.at(3) << 8) + (quint16)buffer.at(4));

				de->io.alexForwardVolts = (qreal)(3.3 * (qreal)de->io.ccRx.ain1 / 4095.0);
				de->io.alexForwardPower = (qreal)(de->io.alexForwardVolts * de->io.alexForwardVolts / 0.09);
			}
			//qDebug() << "alexForwardVolts: " << io.alexForwardVolts << "alexForwardPower" << io.alexForwardPower;
            break;

		case 2:

			//qDebug() << "CC: " << io.ccRx.roundRobin;
			// reverse power
			if (set->getAlexPresence()) { //|| set->getApolloPresence()) {

				de->io.ccRx.ain2 = (quint16)((quint16)(buffer.at(1) << 8) + (quint16)buffer.at(2));

				de->io.alexReverseVolts = (qreal)(3.3 * (qreal)de->io.ccRx.ain2 / 4095.0);
				de->io.alexReversePower = (qreal)(de->io.alexReverseVolts * de->io.alexReverseVolts / 0.09);
			}
			//qDebug() << "alexReverseVolts: " << io.alexReverseVolts << "alexReversePower" << io.alexReversePower;

			if (set->getPenelopePresence() || (m_hwInterface == QSDR::Hermes)) { // || set->getPennyLanePresence() {

				de->io.ccRx.ain3 = (quint16)((quint16)(buffer.at(3) << 8) + (quint16)buffer.at(4));
				de->io.ain3Volts = (qreal)(3.3 * (double)de->io.ccRx.ain3 / 4095.0);
			}
			//qDebug() << "ain3Volts: " << io.ain3Volts;
			break;

		case 3:

			//qDebug() << "CC: " << io.ccRx.roundRobin;

			if (set->getPenelopePresence() || (m_hwInterface == QSDR::Hermes)) { // || set->getPennyLanePresence() {

				de->io.ccRx.ain4 = (quint16)((quint16)(buffer.at(1) << 8) + (quint16)buffer.at(2));
				de->io.ccRx.ain6 = (quint16)((quint16)(buffer.at(3) << 8) + (quint16)buffer.at(4));

				de->io.ain4Volts = (qreal)(3.3 * (qreal)de->io.ccRx.ain4 / 4095.0);

				if (set->getHWInterface() == QSDR::Hermes) // read supply volts applied to board
					de->io.supplyVolts = (qreal)((qreal)de->io.ccRx.ain6 / 186.0f);
			}
			//qDebug() << "ain4Volts: " << io.ain4Volts << "supplyVolts" << io.supplyVolts;
			break;

		//case 4:

			// more than 1 Mercury module (currently not usable)
			//qDebug() << "CC: " << io.ccRx.roundRobin;
			//switch (io.receivers) {

			//	case 1:
			//		io.ccRx.mercury1_LT2208 = (bool)((buffer.at(1) & 0x02) == 0x02);
			//		//qDebug() << "mercury1_LT2208: " << io.ccRx.mercury1_LT2208;
			//		break;

			//	case 2:
			//		io.ccRx.mercury1_LT2208 = (bool)((buffer.at(1) & 0x02) == 0x02);
			//		io.ccRx.mercury2_LT2208 = (bool)((buffer.at(2) & 0x02) == 0x02);
			//		//qDebug() << "mercury1_LT2208: " << io.ccRx.mercury1_LT2208 << "mercury2_LT2208" << io.ccRx.mercury2_LT2208;
			//		break;

			//	case 3:
			//		io.ccRx.mercury1_LT2208 = (bool)((buffer.at(1) & 0x02) == 0x02);
			//		io.ccRx.mercury2_LT2208 = (bool)((buffer.at(2) & 0x02) == 0x02);
			//		io.ccRx.mercury3_LT2208 = (bool)((buffer.at(3) & 0x02) == 0x02);
			//		//qDebug() << "mercury1_LT2208: " << io.ccRx.mercury1_LT2208 << "mercury2_LT2208" << io.ccRx.mercury2_LT2208;
			//		//qDebug() << "mercury3_LT2208: " << io.ccRx.mercury3_LT2208;
			//		break;

			//	case 4:
			//		io.ccRx.mercury1_LT2208 = (bool)((buffer.at(1) & 0x02) == 0x02);
			//		io.ccRx.mercury2_LT2208 = (bool)((buffer.at(2) & 0x02) == 0x02);
			//		io.ccRx.mercury3_LT2208 = (bool)((buffer.at(3) & 0x02) == 0x02);
			//		io.ccRx.mercury4_LT2208 = (bool)((buffer.at(4) & 0x02) == 0x02);
			//		//qDebug() << "mercury1_LT2208: " << io.ccRx.mercury1_LT2208 << "mercury2_LT2208" << io.ccRx.mercury2_LT2208;
			//		//qDebug() << "mercury3_LT2208: " << io.ccRx.mercury3_LT2208 << "mercury4_LT2208" << io.ccRx.mercury4_LT2208;
			//		break;
			//}
			//break;
	} // end switch cycle through C0
}

void DataProcessor::setOutputBuffer(int rx, const CPX &buffer) {

	if (rx == de->io.currentReceiver) {
		processOutputBuffer(buffer);
	}
}

void DataProcessor::processOutputBuffer(const CPX &buffer) {

	//DATA_PROCESSOR_DEBUG << "processOutputBuffer: " << this->thread();

	qint16 leftRXSample;
    qint16 rightRXSample;
    qint16 leftTXSample;
    qint16 rightTXSample;

	// process the output
	for (int j = 0; j < BUFFER_SIZE; j += de->io.outputMultiplier) {

		leftRXSample  = (qint16)(buffer.at(j).re * 32767.0f);
		rightRXSample = (qint16)(buffer.at(j).im * 32767.0f);

		leftTXSample = 0;
        rightTXSample = 0;

		de->io.output_buffer[m_idx++] = leftRXSample  >> 8;
        de->io.output_buffer[m_idx++] = leftRXSample;
        de->io.output_buffer[m_idx++] = rightRXSample >> 8;
        de->io.output_buffer[m_idx++] = rightRXSample;
        de->io.output_buffer[m_idx++] = leftTXSample  >> 8;
        de->io.output_buffer[m_idx++] = leftTXSample;
        de->io.output_buffer[m_idx++] = rightTXSample >> 8;
        de->io.output_buffer[m_idx++] = rightTXSample;
		
		if (m_idx == IO_BUFFER_SIZE) {

			//if (de->m_audioBuffer.length() == 1024) {

			//	//m_audioEngine->setAudioBuffer(this, m_audioBuffer);
			//	de->m_audioBuffer.resize(0);
			//}
			// set the C&C bytes
			encodeCCBytes();

			switch (m_hwInterface) {

				case QSDR::Metis:
				case QSDR::Hermes:

					de->io.audioDatagram.resize(IO_BUFFER_SIZE);
					de->io.audioDatagram = QByteArray::fromRawData((const char *)&de->io.output_buffer, IO_BUFFER_SIZE);
			
					//if (m_dataIOThreadRunning) {
					//	de->m_dataIO->writeData();
					//}

					de->m_dataIO->sendAudio(de->io.output_buffer); //RRK

					writeData();
					break;
			
				case QSDR::NoInterfaceMode:
					break;
			}
			m_idx = IO_HEADER_SIZE;
		}
	}
}

void DataProcessor::encodeCCBytes() {

    de->io.output_buffer[0] = SYNC;
    de->io.output_buffer[1] = SYNC;
    de->io.output_buffer[2] = SYNC;
	
    de->io.mutex.lock();
    switch (m_sendState) {

    	case 0:

    		uchar rxAnt;
    		uchar rxOut;
    		uchar ant;

    		de->io.control_out[0] = 0x0; // C0
    		de->io.control_out[1] = 0x0; // C1
    		de->io.control_out[2] = 0x0; // C2
    		de->io.control_out[3] = 0x0; // C3
    		de->io.control_out[4] = 0x0; // C4

    		// C0
    		// 0 0 0 0 0 0 0 0
    		//               |
    		//               +------------ MOX (1 = active, 0 = inactive)

    		// set C1
    		//
    		// 0 0 0 0 0 0 0 0
    		// | | | | | | | |
    		// | | | | | | + +------------ Speed (00 = 48kHz, 01 = 96kHz, 10 = 192kHz)
    		// | | | | + +---------------- 10MHz Ref. (00 = Atlas/Excalibur, 01 = Penelope, 10 = Mercury)*
    		// | | | +-------------------- 122.88MHz source (0 = Penelope, 1 = Mercury)*
    		// | + +---------------------- Config (00 = nil, 01 = Penelope, 10 = Mercury, 11 = both)*
    		// +-------------------------- Mic source (0 = Janus, 1 = Penelope)*
    		//
   			// * Ignored by Hermes

    		de->io.control_out[1] |= de->io.speed; // sample rate

    		de->io.control_out[1] &= 0x03; // 0 0 0 0 0 0 1 1
    		de->io.control_out[1] |= de->io.ccTx.clockByte;

    		// set C2
    		//
    		// 0 0 0 0 0 0 0 0
    		// |           | |
    		// |           | +------------ Mode (1 = Class E, 0 = All other modes)
    		// +---------- +-------------- Open Collector Outputs on Penelope or Hermes (bit 6...bit 0)

    		de->io.control_out[2] = de->io.rxClass;

    		if (de->io.ccTx.pennyOCenabled) {

    			de->io.control_out[2] &= 0x1; // 0 0 0 0 0 0 0 1

    			if (de->io.ccTx.currentBand != (HamBand) gen) {

    				if (de->io.ccTx.mox || de->io.ccTx.ptt)
    					de->io.control_out[2] |= (de->io.ccTx.txJ6pinList.at(de->io.ccTx.currentBand) >> 1) << 1;
    				else
    					de->io.control_out[2] |= (de->io.ccTx.rxJ6pinList.at(de->io.ccTx.currentBand) >> 1) << 1;
    			}
    		}


    		// set C3
    		//
    		// 0 0 0 0 0 0 0 0
    		// | | | | | | | |
    		// | | | | | | + +------------ Alex Attenuator (00 = 0dB, 01 = 10dB, 10 = 20dB, 11 = 30dB)
    		// | | | | | +---------------- Preamp On/Off (0 = Off, 1 = On)
    		// | | | | +------------------ LT2208 Dither (0 = Off, 1 = On)
    		// | | | + ------------------- LT2208 Random (0= Off, 1 = On)
    		// | + + --------------------- Alex Rx Antenna (00 = none, 01 = Rx1, 10 = Rx2, 11 = XV)
    		// + ------------------------- Alex Rx out (0 = off, 1 = on). Set if Alex Rx Antenna > 00.


    		rxAnt = 0x07 & (de->io.ccTx.alexStates.at(de->io.ccTx.currentBand) >> 2);
    		rxOut = (rxAnt > 0) ? 1 : 0;

    		de->io.control_out[3] = (de->io.ccTx.alexStates.at(de->io.ccTx.currentBand) >> 7);

    		de->io.control_out[3] &= 0xFB; // 1 1 1 1 1 0 1 1
    		de->io.control_out[3] |= (de->io.ccTx.mercuryAttenuator << 2);

    		de->io.control_out[3] &= 0xF7; // 1 1 1 1 0 1 1 1
    		de->io.control_out[3] |= (de->io.ccTx.dither << 3);

    		de->io.control_out[3] &= 0xEF; // 1 1 1 0 1 1 1 1
    		de->io.control_out[3] |= (de->io.ccTx.random << 4);

    		de->io.control_out[3] &= 0x9F; // 1 0 0 1 1 1 1 1
    		de->io.control_out[3] |= rxAnt << 5;

    		de->io.control_out[3] &= 0x7F; // 0 1 1 1 1 1 1 1
    		de->io.control_out[3] |= rxOut << 7;

    		// set C4
    		//
    		// 0 0 0 0 0 0 0 0
    		// | | | | | | | |
    		// | | | | | | + + ----------- Alex Tx relay (00 = Tx1, 01= Tx2, 10 = Tx3)
    		// | | | | | + --------------- Duplex (0 = off, 1 = on)
    		// + + + + +------------------ Number of Receivers (000 = 1, 11111 = 32)

                //RRK removed 4HL
            // | +------------------------ Time stamp - 1PPS on LSB of Mic data (0 = off, 1 = on)
    		// +-------------------------- Common Mercury Frequency (0 = independent frequencies to Mercury
    		//			                   Boards, 1 = same frequency to all Mercury boards)

    		if (de->io.ccTx.mox || de->io.ccTx.ptt)
    			ant = (de->io.ccTx.alexStates.at(de->io.ccTx.currentBand) >> 5);
    		else
    			ant = de->io.ccTx.alexStates.at(de->io.ccTx.currentBand);

    		de->io.control_out[4] |= (ant != 0) ? ant-1 : ant;

    		de->io.control_out[4] &= 0xFB; // 1 1 1 1 1 0 1 1
    		de->io.control_out[4] |= de->io.ccTx.duplex << 2;

    		de->io.control_out[4] &= 0x07; // 0 0 0 0 0 1 1 1
    		de->io.control_out[4] |= (de->io.receivers - 1) << 3;

    		//RRK removed 4HL
    		//de->io.control_out[4] &= 0xBF; // 1 0 1 1 1 1 1 1
    		//de->io.control_out[4] |= de->io.ccTx.timeStamp << 6;

    		//de->io.control_out[4] &= 0x7F; // 0 1 1 1 1 1 1 1
    		//de->io.control_out[4] |= de->io.ccTx.commonMercuryFrequencies << 7;

    		// fill the out buffer with the C&C bytes
    		for (int i = 0; i < 5; i++)
    			de->io.output_buffer[i+3] = de->io.control_out[i];

    		m_sendState = 1;
    		break;

    	case 1:

    		// C0
    		// 0 0 0 0 0 0 1 x     C1, C2, C3, C4 NCO Frequency in Hz for Transmitter, Apollo ATU
    		//                     (32 bit binary representation - MSB in C1)

    		de->io.output_buffer[3] = 0x2; // C0

    		if (de->io.tx_freq_change >= 0) {

    		    de->io.output_buffer[4] = de->RX.at(de->io.tx_freq_change)->getCtrFrequency() >> 24;
    		    de->io.output_buffer[5] = de->RX.at(de->io.tx_freq_change)->getCtrFrequency() >> 16;
    		    de->io.output_buffer[6] = de->RX.at(de->io.tx_freq_change)->getCtrFrequency() >> 8;
    		    de->io.output_buffer[7] = de->RX.at(de->io.tx_freq_change)->getCtrFrequency();

    		    de->io.tx_freq_change = -1;
    		}

    		m_sendState = de->io.ccTx.duplex ? 2 : 3;
    		break;

    	case 2:

    		// C0 = 0 0 0 0 0 1 0 x     C1, C2, C3, C4   NCO Frequency in Hz for Receiver_1
    		// C0 = 0 0 0 0 0 1 1 x     C1, C2, C3, C4   NCO Frequency in Hz for Receiver _2
    		// C0 = 0 0 0 0 1 0 0 x     C1, C2, C3, C4   NCO Frequency in Hz for Receiver _3
    		// C0 = 0 0 0 0 1 0 1 x     C1, C2, C3, C4   NCO Frequency in Hz for Receiver _4
    		// C0 = 0 0 0 0 1 1 0 x     C1, C2, C3, C4   NCO Frequency in Hz for Receiver _5
    		// C0 = 0 0 0 0 1 1 1 x     C1, C2, C3, C4   NCO Frequency in Hz for Receiver _6
    		// C0 = 0 0 0 1 0 0 0 x     C1, C2, C3, C4   NCO Frequency in Hz for Receiver _7
    		// C0 = 0 0 1 0 0 1 0 x     C1, C2, C3, C4   NCO Frequency in Hz for Receiver _8 // Was 0 0 0 1 0 0 1 x
    		// C0 = 0 0 1 1 0 1 0 x     C1, C2, C3, C4   NCO Frequency in Hz for Receiver _16
    		// C0 = 0 1 0 1 0 1 0 x     C1, C2, C3, C4   NCO Frequency in Hz for Receiver _32

    		if (de->io.rx_freq_change >= 0) {

    			de->io.output_buffer[3] = (de->io.rx_freq_change < 7) ? (de->io.rx_freq_change + 2) << 1
    									      : (de->io.rx_freq_change + 11) << 1;
    			//RRK removed 4HL de->io.output_buffer[3] = (de->io.rx_freq_change + 2) << 1;
    			de->io.output_buffer[4] = de->RX.at(de->io.rx_freq_change)->getCtrFrequency() >> 24;
    			de->io.output_buffer[5] = de->RX.at(de->io.rx_freq_change)->getCtrFrequency() >> 16;
    			de->io.output_buffer[6] = de->RX.at(de->io.rx_freq_change)->getCtrFrequency() >> 8;
    			de->io.output_buffer[7] = de->RX.at(de->io.rx_freq_change)->getCtrFrequency();

    			de->io.rx_freq_change = -1;
    		}

    		m_sendState = 3;
    		break;

    	case 3:

    		de->io.control_out[0] = 0x12; // 0 0 0 1 0 0 1 0
    		de->io.control_out[1] = 0x0; // C1
    		de->io.control_out[2] = 0x0; // C2
    		de->io.control_out[3] = 0x0; // C3
    		de->io.control_out[4] = 0x0; // C4

    		// C1
    		// 0 0 0 0 0 0 0 0
    		// |             |
    		// +-------------+------------ Hermes/PennyLane Drive Level (0-255) (ignored by Penelope)


    		// C2
    		// 0 0 0 0 0 0 0 0
    		// | | | | | | | |
    		// | | | | | | | +------------ Hermes/Metis Penelope Mic boost (0 = 0dB, 1 = 20dB)
    		// | | | | | | +-------------- Metis/Penelope or PennyLane Mic/Line-in (0 = mic, 1 = Line-in)
            // | | | | | +---------------- Hermes - Enable/disable Apollo filter (0 = disable, 1 = enable)
            // | | | | +------------------ Hermes - Enable/disable Apollo tuner (0 = disable, 1 = enable)
            // | | | +-------------------- Hermes - Apollo auto tune (0 = end, 1 = start)
            // | | +---------------------- Hermes - select filter board (0 = Alex, 1 = Apollo)
    		// | +------------------------ Alex   - manual HPF/LPF filter select (0 = disable, 1 = enable)
    		// +-------------------------- VNA mode (0 = off, 1 = on)

    		// Alex configuration:
    		//
    		// manual 		  0

    		de->io.control_out[2] &= 0xBF; // 1 0 1 1 1 1 1 1
    		de->io.control_out[2] |= (de->io.ccTx.alexConfig & 0x01) << 6;

    		// C3
    		// 0 0 0 0 0 0 0 0
    		//   | | | | | | |
    		//   | | | | | | +------------ Alex   -	select 13MHz  HPF (0 = disable, 1 = enable)*
    		//   | | | | | +-------------- Alex   -	select 20MHz  HPF (0 = disable, 1 = enable)*
    		//   | | | | +---------------- Alex   -	select 9.5MHz HPF (0 = disable, 1 = enable)*
    		//   | | | +------------------ Alex   -	select 6.5MHz HPF (0 = disable, 1 = enable)*
    		//   | | +-------------------- Alex   -	select 1.5MHz HPF (0 = disable, 1 = enable)*
    		//   | +---------------------- Alex   -	Bypass all HPFs   (0 = disable, 1 = enable)*
    		//   +------------------------ Alex   -	6M low noise amplifier (0 = disable, 1 = enable)*
    		//
    		// *Only valid when Alex - manual HPF/LPF filter select is enabled

    		de->io.control_out[3] &= 0xFE; // 1 1 1 1 1 1 1 0
    		// HPF 13 MHz: 1 0 0 0 0 0 0
    		de->io.control_out[3] |= (de->io.ccTx.alexConfig & 0x40) >> 6;

    		de->io.control_out[3] &= 0xFD; // 1 1 1 1 1 1 0 1
    		// HPF 20 MHz: 1 0 0 0 0 0 0 0
    		de->io.control_out[3] |= (de->io.ccTx.alexConfig & 0x80) >> 6;

    		de->io.control_out[3] &= 0xFB; // 1 1 1 1 1 0 1 1
    		// HPF 9.5 MHz: 1 0 0 0 0 0
    		de->io.control_out[3] |= (de->io.ccTx.alexConfig & 0x20) >> 3;

    		de->io.control_out[3] &= 0xF7; // 1 1 1 1 0 1 1 1
    		// HPF 6.5 MHz: 1 0 0 0 0
    		de->io.control_out[3] |= (de->io.ccTx.alexConfig & 0x10) >> 1;

    		de->io.control_out[3] &= 0xEF; // 1 1 1 0 1 1 1 1
    		// HPF 1.5 MHz: 1 0 0 0
    		de->io.control_out[3] |= (de->io.ccTx.alexConfig & 0x08) << 1;

    		de->io.control_out[3] &= 0xDF; // 1 1 0 1 1 1 1 1
    		// bypass all: 1 0
    		de->io.control_out[3] |= (de->io.ccTx.alexConfig & 0x02) << 4;

    		de->io.control_out[3] &= 0xBF; // 1 0 1 1 1 1 1 1
    		// 6m BPF/LNA: 1 0 0
    		de->io.control_out[3] |= (de->io.ccTx.alexConfig & 0x04) << 4;

    		de->io.control_out[3] &= 0x7F; // 0 1 1 1 1 1 1 1
    		de->io.control_out[3] |= ((int)de->io.ccTx.vnaMode) << 7;

    		// C4
    		// 0 0 0 0 0 0 0 0
    		//   | | | | | | |
    		//   | | | | | | +------------ Alex   - 	select 30/20m LPF (0 = disable, 1 = enable)*
    		//   | | | | | +-------------- Alex   - 	select 60/40m LPF (0 = disable, 1 = enable)*
    		//   | | | | +---------------- Alex   - 	select 80m    LPF (0 = disable, 1 = enable)*
    		//   | | | +------------------ Alex   - 	select 160m   LPF (0 = disable, 1 = enable)*
    		//   | | +-------------------- Alex   - 	select 6m     LPF (0 = disable, 1 = enable)*
    		//   | +---------------------- Alex   - 	select 12/10m LPF (0 = disable, 1 = enable)*
    		//   +------------------------ Alex   - 	select 17/15m LPF (0 = disable, 1 = enable)*
    		//
    		// *Only valid when Alex - manual HPF/LPF filter select is enabled

    		de->io.control_out[4] &= 0xFE; // 1 1 1 1 1 1 1 0
    		// LPF 30/20m: 1 0 0 0 0 0 0 0 0 0 0 0
    		de->io.control_out[4] |= (de->io.ccTx.alexConfig & 0x800) >> 11;

    		de->io.control_out[4] &= 0xFD; // 1 1 1 1 1 1 0 1
    		// LPF 60/40m: 1 0 0 0 0 0 0 0 0 0 0
    		de->io.control_out[4] |= (de->io.ccTx.alexConfig & 0x400) >> 9;

    		de->io.control_out[4] &= 0xFB; // 1 1 1 1 1 0 1 1
    		// LPF 80m: 1 0 0 0 0 0 0 0 0 0
    		de->io.control_out[4] |= (de->io.ccTx.alexConfig & 0x200) >> 7;

    		de->io.control_out[4] &= 0xF7; // 1 1 1 1 0 1 1 1
    		// LPF 160m: 1 0 0 0 0 0 0 0 0
    		de->io.control_out[4] |= (de->io.ccTx.alexConfig & 0x100) >> 5;

    		de->io.control_out[4] &= 0xEF; // 1 1 1 0 1 1 1 1
    		// LPF 6m: 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0
    		de->io.control_out[4] |= (de->io.ccTx.alexConfig & 0x4000) >> 10;

    		de->io.control_out[4] &= 0xDF; // 1 1 0 1 1 1 1 1
    		// LPF 12/10m : 1 0 0 0 0 0 0 0 0 0 0 0 0 0
    		de->io.control_out[4] |= (de->io.ccTx.alexConfig & 0x2000) >> 8;

    		de->io.control_out[4] &= 0xBF; // 1 0 1 1 1 1 1 1
    		// LPF 17/15m: 1 0 0 0 0 0 0 0 0 0 0 0 0
    		de->io.control_out[4] |= (de->io.ccTx.alexConfig & 0x1000) >> 6;

    		// fill the out buffer with the C&C bytes
    		for (int i = 0; i < 5; i++)
    			de->io.output_buffer[i+3] = de->io.control_out[i];

		// check if we need to update ADC C&C's
		new_adc_rx1_4 = new_adc_rx5_8 = new_adc_rx9_16 = 0;
		for (int i = 0; i < set->getNumberOfReceivers(); i++) {
			if (i < 4) new_adc_rx1_4 |= de->RX.at(i)->getADCMode() << (i * 2);
			else if (i < 8) new_adc_rx5_8 |= de->RX.at(i)->getADCMode() << ((i-4) * 2);
			else if (i < 16) new_adc_rx9_16 |= de->RX.at(i)->getADCMode() << (i-8);
		}

		if ((new_adc_rx1_4 != adc_rx1_4) || (new_adc_rx5_8 != adc_rx5_8) || (new_adc_rx9_16 != adc_rx9_16))
    			m_sendState = 4;
		else
    			m_sendState = 0;
    		break;

    	case 4:
		// setup data for ADC c&c's
		adc_rx1_4 = new_adc_rx1_4;
		adc_rx5_8 = new_adc_rx5_8;
		adc_rx9_16 = new_adc_rx9_16;

		de->io.control_out[0] = 0x1C; // 0 0 0 1 1 1 0 x
    		de->io.control_out[1] = adc_rx1_4; // C1
    		de->io.control_out[2] = adc_rx5_8; // C2
    		de->io.control_out[3] = 0x0; // C3, ADC Input Attenuator Tx (0-31dB) [4:0]
		de->io.control_out[4] = adc_rx9_16; // C4

   		// fill the out buffer with the C&C bytes
		for (int i = 0; i < 5; i++)
			de->io.output_buffer[i+3] = de->io.control_out[i];

		//DATA_PROCESSOR_DEBUG << "rx_adc_change rcvr: " << hex << new_adc_rx1_4 << "," << hex << new_adc_rx5_8 << "," << hex << new_adc_rx9_16;

    		// round finished
    		m_sendState = 0;
    		break;

    }
    de->io.mutex.unlock();


	/*switch (m_hwInterface) {

		case QSDR::Metis:
		case QSDR::Hermes:

			io.audioDatagram.resize(IO_BUFFER_SIZE);
			io.audioDatagram = QByteArray::fromRawData((const char *)&io.output_buffer, IO_BUFFER_SIZE);
			
			if (m_dataIOThreadRunning) {
				m_dataIO->writeData();
			}
			break;
			
		case QSDR::NoInterfaceMode:
			break;
	}*/
}

void DataProcessor::writeData() {

	if (m_setNetworkDeviceHeader) {

		//RRK updated for 4byte int and network order
		quint32 outseq = qFromBigEndian(m_sendSequence);
		m_outDatagram.resize(0);
        m_outDatagram += m_deviceSendDataSignature;

		QByteArray seq(reinterpret_cast<const char*>(&outseq), sizeof(outseq));

		m_outDatagram += seq;
		m_outDatagram += de->io.audioDatagram;

		m_sendSequence++;
        m_setNetworkDeviceHeader = false;
    }
	else {

		//QUdpSocket socket;
		//DATA_PROCESSOR_DEBUG << "writeData: " << this->thread();
		m_outDatagram += de->io.audioDatagram;
		
		if (de->sendSocket->writeDatagram(m_outDatagram, m_deviceAddress, DEVICE_PORT) < 0) {
			DATA_PROCESSOR_DEBUG << "error sending data to device: " << de->sendSocket->errorString();
		}

		//if (m_sendSequence%100 == 0)
		//	DATAIO_DEBUG << m_sendSequence;

		if (m_sendSequence != m_oldSendSequence + 1) {
			DATA_PROCESSOR_DEBUG << "output sequence error: old = " << m_oldSendSequence << "; new =" << m_sendSequence;
		}

		m_oldSendSequence = m_sendSequence;
		m_setNetworkDeviceHeader = true;
    }
}


// *********************************************************************
// Audio out processor

AudioOutProcessor::AudioOutProcessor(DataEngine *de, QSDR::_ServerMode serverMode)
	: QObject()
	, m_dataEngine(de)
	, m_serverMode(serverMode)
	, m_stopped(false)
{	
	m_IQDatagram.resize(0);
}

AudioOutProcessor::~AudioOutProcessor() {
}

void AudioOutProcessor::stop() {

	m_stopped = true;
}

void AudioOutProcessor::processDeviceData() {

	forever {

		//m_dataEngine->processInputBuffer(m_dataEngine->io.iq_queue.dequeue());
		//DATA_ENGINE_DEBUG << "IQ queue length:" << m_dataEngine->io.iq_queue.count();
		//DATA_ENGINE_DEBUG << "iq_queue length:" << m_dataEngine->io.iq_queue.dequeue().length();
		
		m_mutex.lock();
		if (m_stopped) {
			m_stopped = false;
			m_mutex.unlock();
			break;
		}
		m_mutex.unlock();
	}
}

void AudioOutProcessor::processData() {

	forever {

		//m_dataEngine->processFileBuffer(m_dataEngine->io.data_queue.dequeue());

		m_mutex.lock();
		if (m_stopped) {
			m_stopped = false;
			m_mutex.unlock();
			break;
		}
		m_mutex.unlock();
	}
}


// *********************************************************************
// Wide band data processor
 
WideBandDataProcessor::WideBandDataProcessor(THPSDRParameter *ioData, QSDR::_ServerMode serverMode, int size)
	: QObject()
	, io(ioData)
	, set(Settings::instance())
	, m_serverMode(serverMode)
	, m_size(size)
	, m_bytes(0)
	, m_wbSpectrumAveraging(true)
	, m_stopped(false)
{
	m_WBDatagram.resize(0);

	switch (m_serverMode) {
		
		case QSDR::SDRMode:

			wbFFT = new QFFT(m_size);
			
			cpxWBIn.resize(m_size);
			cpxWBOut.resize(m_size);

			io->wbWindow.resize(m_size);
			io->wbWindow.fill(0.0f);
			
			QFilter::MakeWindow(12, m_size, (float *)io->wbWindow.data()); // 12 = BLACKMANHARRIS_WINDOW

			wbAverager = new DualModeAverager(-1, m_size/2);

			break;

		//case QSDR::ExternalDSP:
		case QSDR::ChirpWSPR:
		case QSDR::ChirpWSPRFile:
			break;
			
		case QSDR::NoServerMode:
		case QSDR::DemoMode:
			break;
	}
}

WideBandDataProcessor::~WideBandDataProcessor() {

	delete wbFFT;
	
	if (wbAverager) {

		delete wbAverager;
	}

	cpxWBIn.clear();
	cpxWBOut.clear();
}

void WideBandDataProcessor::stop() {

	//mutex.lock();
	m_stopped = true;
	//mutex.unlock();
}

void WideBandDataProcessor::processWideBandData() {

	forever {

		processWideBandInputBuffer(io->wb_queue.dequeue());
		
		m_mutex.lock();
		if (m_stopped) {
			m_stopped = false;
			m_mutex.unlock();
			break;
		}
		m_mutex.unlock();
	}
}

void WideBandDataProcessor::processWideBandInputBuffer(const QByteArray &buffer) {

	int size;

	//if (m_mercuryFW > 32 || m_hermesFW > 16)
	if (io->mercuryFW > 32 || io->hermesFW > 11)
		size = 2 * BIGWIDEBANDSIZE;
	else
		size = 2 * SMALLWIDEBANDSIZE;

	qint64 length = buffer.length();
	if (buffer.length() != size) {

		WIDEBAND_PROCESSOR_DEBUG << "wrong wide band buffer length: " << length;
		return;
	}

	int s;
	float sample;
	float norm = 1.0f / (4 * size);

	for (int i = 0; i < length; i += 2) {

		s =  (int)((qint8 ) buffer.at(i+1)) << 8;
		s += (int)((quint8) buffer.at(i));
		sample = (float)(s * norm);

		cpxWBIn[i/2].re = sample * io->wbWindow.at(i/2);
		cpxWBIn[i/2].im = sample * io->wbWindow.at(i/2);
	}

	wbFFT->DoFFTWForward(cpxWBIn, cpxWBOut, size/2);

	// averaging
	QVector<float> specBuf(size/4);

	m_mutex.lock();
	if (m_wbSpectrumAveraging) {

		for (int i = 0; i < size/4; i++)
			specBuf[i] = (float)(10.0 * log10(MagCPX(cpxWBOut.at(i)) + 1.5E-45));

		wbAverager->ProcessDBAverager(specBuf, specBuf);
		m_mutex.unlock();
	}
	else {

		for (int i = 0; i < size/4; i++)
			specBuf[i] = (float)(10.0 * log10(MagCPX(cpxWBOut.at(i)) + 1.5E-45));

		m_mutex.unlock();
	}

	//set->setWidebandSpectrumBuffer(specBuf);
	emit wbSpectrumBufferChanged(specBuf);
}

void WideBandDataProcessor::setWbSpectrumAveraging(QObject* sender, int rx, bool value) {

	Q_UNUSED (sender)

	if (rx != -1) return;

	m_mutex.lock();
	m_wbSpectrumAveraging = value;
	m_mutex.unlock();
}
