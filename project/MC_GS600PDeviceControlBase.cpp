#include "MC_GS600PDeviceControlBase.h"
#include "MC_OpcUaClient.h"
#include "MT_Transition.h"
#include "ML_GlobalLog.h"
#include "MA_Auxiliary.h"
#include <QStateMachine>
#include <QState>
#include <QTimer>
#include <QMutexLocker>


using MP_Public::MM_MaybeOk;
using MP_Public::MM_Maybe;
using MP_Public::ME_Error;


MC_GS600PDeviceControlBase::MC_GS600PDeviceControlBase(QObject *_parent)
	: ML_LogBase(_parent), MS_StateMachineAuxiliary(),
	m_client(new MC_OpcUaClient()),
	m_onClientRequireDataMachine(new QStateMachine(this)),
	m_onClientUploadDataMachine(new QStateMachine(this)),
	m_onCheckRequireDataTimer(new QTimer(this)),
	m_onCheckRequireUploadTimer(new QTimer(this))
{


	m_transitionKeyWordMap[ME_TransitionKeyWordType::BeReadyState] = MI_Device::s_transitionToolingBeReadyStateName;
	m_transitionKeyWordMap[ME_TransitionKeyWordType::BeInPlanningState] = MI_Device::s_transitionToolingBeInPlanningStateName;
	m_transitionKeyWordMap[ME_TransitionKeyWordType::BeInPlanningRespond] = MI_Device::s_transitionToolingBeInPlanningRespondName;
	m_transitionKeyWordMap[ME_TransitionKeyWordType::Command] = MI_Device::s_transitionToolingCommandName;
	m_transitionKeyWordMap[ME_TransitionKeyWordType::ExecuteState] = MI_Device::s_transitionToolingExecuteStateName;
	m_transitionKeyWordMap[ME_TransitionKeyWordType::FinishResult] = MI_Device::s_transitionToolingFinishResultName;

	QObject::connect(m_client.get(), &MC_OpcUaClient::sig_connectResult, this, [=](const MM_MaybeOk& _val)
	{
		emit sig_connectResult(_val);
		if (!_val.hasError())
		{
			clearConnectionMakeWhenConnection();
			Q_ASSERT(m_client);
			m_client->clearMonitorWords();
			makeNodesValChangedConnections();

		}
	});


	QObject::connect(m_client.get(), &MC_OpcUaClient::sig_connectStateChanged, this, [=](MS_ConnectState _val)
	{
		emit this->sig_connectStateChanged(_val);
	});


	QObject::connect(m_client.get(), &MC_OpcUaClient::sig_log, this, [=](ML_LogLabel _label, const QString & _log) {
		emit sig_log(_label, _log);
	});

	QObject::connect(m_onCheckRequireDataTimer, &QTimer::timeout, this, [=]() {
		if (getDeviceIsRequireDataState() == MS_DeviceRequireDataState::REQUIRE)
		{
			QTimer::singleShot(0, [=]() {
				emit sig_hasReceiveDeviceRequireDataCommand();
			});
		}
	});

	QObject::connect(m_onCheckRequireUploadTimer, &QTimer::timeout, this, [=]() {
		if (getDeviceIsRequireUploadDataState() == MS_DeviceReuireUploadDataState::REQUIRE)
		{
			QTimer::singleShot(0, [=]() {
				emit this->sig_hasReceiveDeviceRequireUploadDataCommand();
			});
		}
	});


	initOnClientRequireDataMachine();
	initOnClinetUploadDataMachine();
}

void MC_GS600PDeviceControlBase::onHasUploadData()
{
	m_onClientUploadDataMachine->postEvent(new MT_TriggerEvent(MT_TriggerEventType::DataUploadFinished));
}

void MC_GS600PDeviceControlBase::makeNodesValChangedConnections()
{
	//初始化指令执行状态
	makeNodeValueChangedConnection(MI_Device::s_initCommandExecuteStateName, [=](quint16 _val)
	{
		setInitCommandExecuteState(_val);
	});


	makeReceiveToolingValConnections();

	makeSendToolingValConnections();

	//工位状态
	makeNodeValueChangedConnection(MI_Device::s_deviceStateName, [=](quint16 _val)
	{
		setDeviceState(_val);
	});

	//是否显示主控
	makeNodeValueChangedConnection(MI_Device::s_deviceIfShowMainControl, [=](quint16 _val)
	{
		setDeviceIfShowMainControlUiState(_val);
	});

	//设备作业区是否有工装
	makeNodeValueChangedConnection(MI_Device::s_deviceWorkAreaIfHasToolingName, [=](quint16 _val)
	{
		setDeviceWorkAreaIfHasToolingState(_val);
	});

	//作业区状态
	makeNodeValueChangedConnection(MI_Device::s_deviceWorkAreaWorkStateName, [=](quint16 _val)
	{
		setDeviceWorkAreaWorkState(_val);
	});

	makeDataConnections();


}

void MC_GS600PDeviceControlBase::makeDataConnections()
{
	//是否请求数据
	makeNodeValueChangedConnection(MI_Device::s_deviceRequireDataCommandName, [=](quint16 _val)
	{
		setDeviceIsRequireDataState(_val);
	});


	//是否上传数据
	makeNodeValueChangedConnection(MI_Device::s_deviceUploadWorkResultDataCommandName, [=](quint16 _val)
	{
		setDeviceIsRequireUploadDataState(_val);
	});

}

void MC_GS600PDeviceControlBase::makeSendToolingValConnections()
{
	//送板就绪
	makeNodeValueChangedConnection(MI_Device::s_isReadyToSendToolingStateName, [=](qint16 _val)
	{
		setDeviceReadyToSendOutState(_val);
	});

	//送板规划应答
	makeNodeValueChangedConnection(MI_Device::s_sendToolingBeInPlanningRespondName, [=](qint16 _val)
	{
		setPlanSendToolingStateRespond(_val);
	});

	//送板指令执行状态
	makeNodeValueChangedConnection(MI_Device::s_sendToolingCommandExecuteStateName, [=](quint16 _val)
	{
		setSendToolingCommandExecuteState(_val);
	});
}

void MC_GS600PDeviceControlBase::makeReceiveToolingValConnections()
{
	//收板就绪
	makeNodeValueChangedConnection(MI_Device::s_isReadyToReceiveToolingStateName, [=](qint16 _val)
	{
		setDeviceReadyToReceiveInState(_val);
	});

	//收板规划应答
	makeNodeValueChangedConnection(MI_Device::s_receiveToolingBeInPlanningRespondName, [=](qint16 _val)
	{
		setPlanReceiveToolingStateRespond(_val);
	});

	//收板指令执行状态
	makeNodeValueChangedConnection(MI_Device::s_receiveToolingCommandExecuteStateName, [=](quint16 _val)
	{
		setReceiveToolingCommandExecuteState(_val);
	});
}

MC_GS600PDeviceControlBase::~MC_GS600PDeviceControlBase()
{
	m_onClientRequireDataMachine->stop();
	m_onClientUploadDataMachine->stop();
}

QDateTime MC_GS600PDeviceControlBase::getDeviceWorkAreaStartWorkDateTime()
{
	QMutexLocker locker(&m_deviceWorkAreaStartWorkDateTimeMutex);
	return m_deviceWorkAreaStartWorkDateTime;
}

void MC_GS600PDeviceControlBase::setDeviceWorkAreaStartWorkDateTime(const QDateTime& _val)
{
	QMutexLocker locker(&m_deviceWorkAreaStartWorkDateTimeMutex);
	m_deviceWorkAreaStartWorkDateTime = _val;
}

void MC_GS600PDeviceControlBase::setDeviceReadyToSendOutState(quint16 _val)
{
	if (getDeviceReadyToSendOutState() == _val)
	{
		return;
	}

	if (_val == MS_IsReadyReceiveAndSendToolingState::HAS_READY) {
		setDeviceReadyToSendOutDateTime(QDateTime::currentDateTime());
	}

	m_deviceReadyToSendOutState = _val;
	emit sig_readyToSendOutToolingStateChanged(_val);
}

void MC_GS600PDeviceControlBase::setDeviceIsRequireUploadDataState(quint16 _val)
{
	if (getDeviceIsRequireUploadDataState() == _val)
	{
		return;
	}
	m_deviceIsRequireUploadDataState = _val;
	if (_val == MS_DeviceReuireUploadDataState::REQUIRE)
	{
		emit this->sig_hasReceiveDeviceRequireUploadDataCommand();
	}
}

void MC_GS600PDeviceControlBase::setDeviceReadyToReceiveInState(quint16 _val)
{
	if (getDeviceReadyToReceiveInState() == _val)
	{
		return;
	}

	if (_val == MS_IsReadyReceiveAndSendToolingState::HAS_READY) {
		setDeviceReadyToReceiveInDateTime(QDateTime::currentDateTime());
	}
	m_deviceReadyToReceiveInState = _val;

	emit sig_readyToReceiveInToolingStateChanged(_val);
}

void MC_GS600PDeviceControlBase::setDeviceIsRequireDataState(quint16 _val)
{
	if (getDeviceIsRequireDataState() == _val)
	{
		return;
	}
	m_deviceIsRequireDataState = _val;
	if (_val == MS_DeviceRequireDataState::REQUIRE)
	{
		emit sig_hasReceiveDeviceRequireDataCommand();
	}
}

void MC_GS600PDeviceControlBase::initOnClientRequireDataMachine()
{
	auto curMachine = m_onClientRequireDataMachine;
	auto onError = [=]()
	{
		curMachine->postEvent(new MT_TriggerEvent(MT_TriggerEventType::ExistError));
	};
	//第一次读取请求数据指令
	auto readRequireDataCommandFirstTimeState = new QState();
	QObject::connect(readRequireDataCommandFirstTimeState, &QState::entered, this, [=]()
	{
		log(ML_LogLabel::NORMAL_LABEL, u8"等待请求数据指令...");
		m_onCheckRequireDataTimer->start(300);
		setStateOnExitAction(curMachine, readRequireDataCommandFirstTimeState, [=]()
		{
			m_onCheckRequireDataTimer->stop();
		});
	});

	QObject::connect(readRequireDataCommandFirstTimeState, &QState::exited, this, [=]() {
		executeStateOnExitAction(curMachine, readRequireDataCommandFirstTimeState);
	});

	//读取工装识别号类型和工装识别号并写执行中状态
	auto startExecuteState = new QState();
	QObject::connect(startExecuteState, &QState::entered, this, [=]()
	{
		log(ML_LogLabel::NORMAL_LABEL, u8"收到请求数据指令,开始执行...");
		std::vector<QString> keyNames;
		keyNames.emplace_back(MI_Device::s_deviceRequireDataToolingIdentifierTypeName);
		keyNames.emplace_back(MI_Device::s_deviceRequireDataToolingIdentifierName);
		readMultiVal(keyNames, [=](ME_Error const& _val)
		{
			log(ML_LogLabel::WARNING_LABEL, _val.getMessage());
			onError();
		}, [=](std::map<QString, QVariant> const& _result)
		{
			//确认是否有数据
			auto curKeyName = MI_Device::s_deviceRequireDataToolingIdentifierTypeName;
			auto typeResultIter = _result.find(curKeyName);
			if (typeResultIter == _result.end())
			{
				log(ML_LogLabel::WARNING_LABEL, QString(u8"请求数据指令 %1 读取失败！").arg(curKeyName));
				onError();
				return;
			}
			//确认数据类型
			if (!typeResultIter->second.canConvert<quint16>())
			{
				log(ML_LogLabel::WARNING_LABEL, QString(u8"请求数据指令 %1 值类型错误！").arg(curKeyName));
				onError();
				return;
			}

			curKeyName = MI_Device::s_deviceRequireDataToolingIdentifierName;
			auto identifierResultIter = _result.find(curKeyName);
			if (identifierResultIter == _result.end())
			{
				log(ML_LogLabel::WARNING_LABEL, QString(u8"请求数据指令 %1 读取失败！").arg(curKeyName));
				onError();
				return;
			}


			if (!identifierResultIter->second.canConvert<QByteArray>())
			{
				log(ML_LogLabel::WARNING_LABEL, QString(u8"请求数据指令 %1 值类型错误！").arg(curKeyName));
				onError();
				return;
			}

			auto identifierType = typeResultIter->second.value<quint16>();
			auto identifier = identifierResultIter->second.value<QByteArray>();

			//写执行状态
			auto watch = m_client->writeNodeVariable(MI_Device::s_deviceRequireDataExecuteStateName, MS_ExecuteState::EXECUTING, QOpcUa::Types::UInt16);
			QObject::connect(watch, &MC_FutureWatchBase::finished, this, [=]()
			{
				log(ML_LogLabel::NORMAL_LABEL, QString(u8"请求数据指令下发数据(识别码类型[%1] 识别码[%2])...")
					.arg(QString::number(identifierType))
					.arg(QString(identifier)));

				ME_DestructExecuter onDeleteObject([=]() {
					watch->deleteLater();
				});
				if (!watch->getIsSuccess())
				{
					log(ML_LogLabel::WARNING_LABEL, QString(u8"请求数据指令下发数据写执行状态失败 : %1").arg(watch->getErrorString()));
					onError();
					return;
				}
				emit sig_deviceRequireData(MI_ToolingIdentifier(identifier, identifierType));
				//等待写入数据
				log(ML_LogLabel::NORMAL_LABEL, u8"请求数据指令等待查找数据...");
			});

		});
	});

	//写执行完成
	auto writeExecuteFinishState = new QState();
	QObject::connect(writeExecuteFinishState, &QState::entered, this, [=]()
	{
		log(ML_LogLabel::NORMAL_LABEL, u8"请求数据指令已下发数据");
		log(ML_LogLabel::NORMAL_LABEL, u8"重置不请求数据指令...");
		//写不请求数据指令
		auto writeNotRequireDataWatch = m_client->writeNodeVariable(MI_Device::s_deviceRequireDataCommandName, MI_SendCommand::NOT_EXECUTE, QOpcUa::Types::UInt16);
		QObject::connect(writeNotRequireDataWatch, &MC_FutureWatchBase::finished, this, [=]()
		{
			ME_DestructExecuter onDeleteObject([=]() {
				writeNotRequireDataWatch->deleteLater();
			});
			if (!writeNotRequireDataWatch->getIsSuccess())
			{
				log(ML_LogLabel::WARNING_LABEL, QString(u8"重置不请求数据指令失败 : %1").arg(writeNotRequireDataWatch->getErrorString()));
				onError();
				return;
			}
			setDeviceIsRequireDataState(MS_DeviceRequireDataState::NOT_REQUIRE);

			//写执行完成
			auto writeExecuteFinishedWatch = m_client->writeNodeVariable(MI_Device::s_deviceRequireDataExecuteStateName, MS_ExecuteState::FINIHED, QOpcUa::Types::UInt16);
			QObject::connect(writeExecuteFinishedWatch, &MC_FutureWatchBase::finished, this, [=]()
			{
				ME_DestructExecuter onDeleteObject([=]() {
					writeExecuteFinishedWatch->deleteLater();
				});
				if (!writeExecuteFinishedWatch->getIsSuccess())
				{
					log(ML_LogLabel::WARNING_LABEL, QString(u8"请求数据指令置完成失败 : %1").arg(writeExecuteFinishedWatch->getErrorString()));
					onError();
					return;
				}
				curMachine->postEvent(new MT_TriggerEvent(MT_TriggerEventType::CommandExecuteFinished));
			});

		});
	});


	auto onFailState = new QState();
	QObject::connect(onFailState, &QState::entered, this, [=]()
	{
		log(ML_LogLabel::WARNING_LABEL, u8"下发数据失败！");
		qDebug() << u8"Execute device require data fail! ";
	});
	auto onSuccessState = new QState();
	QObject::connect(onSuccessState, &QState::entered, this, [=]()
	{
		log(ML_LogLabel::NORMAL_LABEL, u8"下发数据成功！");
		qDebug() << u8"Execute device require data success! ";
	});

	///////////////////////

	readRequireDataCommandFirstTimeState->addTransition(this, &MC_GS600PDeviceControlBase::sig_hasReceiveDeviceRequireDataCommand, startExecuteState);

	///////////
	auto startExecuteSuccessTransition = new MT_Transition(MT_TriggerEventType::CommandWriteDataSuccess);
	startExecuteSuccessTransition->setTargetState(writeExecuteFinishState);
	startExecuteState->addTransition(startExecuteSuccessTransition);

	auto startExecuteFailTransition = new MT_Transition(MT_TriggerEventType::ExistError);
	startExecuteFailTransition->setTargetState(onFailState);
	startExecuteState->addTransition(startExecuteFailTransition);

	/////////////////
	auto writeExecuteFinishSuccessTransition = new MT_Transition(MT_TriggerEventType::CommandExecuteFinished);
	writeExecuteFinishSuccessTransition->setTargetState(onSuccessState);
	writeExecuteFinishState->addTransition(writeExecuteFinishSuccessTransition);

	auto writeExecuteFinishFailTransition = new MT_Transition(MT_TriggerEventType::ExistError);
	writeExecuteFinishFailTransition->setTargetState(onFailState);
	writeExecuteFinishState->addTransition(writeExecuteFinishFailTransition);

	//////////////
	onFailState->addTransition(readRequireDataCommandFirstTimeState);
	onSuccessState->addTransition(readRequireDataCommandFirstTimeState);


	curMachine->addState(readRequireDataCommandFirstTimeState);
	curMachine->setInitialState(readRequireDataCommandFirstTimeState);
	curMachine->addState(startExecuteState);
	curMachine->addState(writeExecuteFinishState);
	curMachine->addState(onFailState);
	curMachine->addState(onSuccessState);

}

void MC_GS600PDeviceControlBase::initOnClinetUploadDataMachine()
{
	auto curMachine = m_onClientUploadDataMachine;
	auto onError = [=]()
	{
		curMachine->postEvent(new MT_TriggerEvent(MT_TriggerEventType::ExistError));
	};
	//第一次读取请求数据指令
	auto readUploadCommandFirstTimeState = new QState();
	QObject::connect(readUploadCommandFirstTimeState, &QState::entered, this, [=]()
	{
		log(ML_LogLabel::NORMAL_LABEL, u8"等待上传数据指令...");
		m_onCheckRequireUploadTimer->start(300);
		setStateOnExitAction(curMachine, readUploadCommandFirstTimeState, [=]()
		{
			m_onCheckRequireUploadTimer->stop();
		});
	});


	QObject::connect(readUploadCommandFirstTimeState, &QState::exited, this, [=]() {
		executeStateOnExitAction(curMachine, readUploadCommandFirstTimeState);
	});
	//检查数据有效标志
	auto checkDataValidState = new QState();
	QObject::connect(checkDataValidState, &QState::entered, this, [=]()
	{
		//读数据是否有效
		log(ML_LogLabel::NORMAL_LABEL, u8"上传数据读取数据有效位...");
		readVal(MI_Device::s_deviceUploadWorkResultDataToolingDataIsValidName,
			[=](ME_Error const& _val)
		{
			log(ML_LogLabel::WARNING_LABEL, u8"上传数据读取数据有效位出错! " + _val.getMessage());
			onError();
		}, [=](QVariant const& _data)
		{
			if (_data.value<quint16>() != MS_DataValidState::IS_VALID)
			{
				log(ML_LogLabel::WARNING_LABEL, u8"上传数据为数据有效位为无效!");
				onError();
			}
			else
			{
				log(ML_LogLabel::NORMAL_LABEL, u8"上传数据检查数据有效!");
				curMachine->postEvent(new MT_TriggerEvent(MT_TriggerEventType::DataIsValid));
			}
		});
	});

	//读数据并置执行中状态
	auto startExecuteState = new QState();
	QObject::connect(startExecuteState, &QState::entered, this, [=]()
	{
		log(ML_LogLabel::NORMAL_LABEL, u8"上传数据读取数据中...");
		std::vector<QString> keyNames;
		keyNames.emplace_back(MI_Device::s_deviceUploadWorkResultDataToolingIdentifierTypeName);
		keyNames.emplace_back(MI_Device::s_deviceUploadWorkResultDataToolingIdentifierName);
		keyNames.emplace_back(MI_Device::s_deviceUploadWorkResultDataToolingIndexName);
		keyNames.emplace_back(MI_Device::s_deviceUploadWorkResultDataContentName);

		auto logReadValFailFun = [=](const QString& _key) {
			log(ML_LogLabel::WARNING_LABEL, QString(u8"读取上传数据:[%1] 读取失败!").arg(_key));
		};

		auto logReadValTypeNotRightFun = [=](const QString& _key) {
			log(ML_LogLabel::WARNING_LABEL, QString(u8"读取上传数据:[%1] 数据类型不正确!").arg(_key));
		};

		readMultiVal(keyNames, [=](ME_Error const& _val)
		{
			log(ML_LogLabel::WARNING_LABEL, _val.getMessage());
			onError();
		}, [=](std::map<QString, QVariant> const& _result)
		{
			//确认是否有数据
			QString curKeyName = MI_Device::s_deviceUploadWorkResultDataToolingIdentifierTypeName;
			auto typeResultIter = _result.find(curKeyName);
			if (typeResultIter == _result.end())
			{
				logReadValFailFun(curKeyName);
				qDebug() << curKeyName << ": is not read!";
				onError();
				return;
			}
			//确认数据类型
			if (!typeResultIter->second.canConvert<quint16>())
			{
				logReadValTypeNotRightFun(curKeyName);
				qDebug() << curKeyName << " : value type is not right!";
				onError();
				return;
			}

			curKeyName = MI_Device::s_deviceUploadWorkResultDataToolingIdentifierName;
			auto identifierResultIter = _result.find(curKeyName);
			if (identifierResultIter == _result.end())
			{
				logReadValFailFun(curKeyName);
				qDebug() << curKeyName << " : is not read!";
				onError();
				return;
			}
			if (!identifierResultIter->second.canConvert<QByteArray>())
			{
				logReadValTypeNotRightFun(curKeyName);
				qDebug() << curKeyName << " : value type is not right!";
				onError();
				return;
			}

			curKeyName = MI_Device::s_deviceUploadWorkResultDataToolingIndexName;
			auto iterToolingIndex = _result.find(curKeyName);
			if (iterToolingIndex == _result.end())
			{
				logReadValFailFun(curKeyName);
				qDebug() << curKeyName << " : is not read! ";
				onError();
				return;
			}
			if (!iterToolingIndex->second.canConvert<quint64>())
			{
				logReadValTypeNotRightFun(curKeyName);
				qDebug() << curKeyName << " : value type is not right!";
				onError();
				return;
			}

			curKeyName = MI_Device::s_deviceUploadWorkResultDataContentName;
			auto iterData = _result.find(curKeyName);
			if (iterData == _result.end())
			{
				logReadValFailFun(curKeyName);
				qDebug() << curKeyName << " : is not read!";
				onError();
				return;
			}
			if (!iterData->second.canConvert<QByteArray>())
			{
				logReadValTypeNotRightFun(curKeyName);
				qDebug() << curKeyName << " : value type is not right!";
				onError();
				return;
			}


			auto identifierType = typeResultIter->second.value<quint16>();
			auto identifier = identifierResultIter->second.value<QByteArray>();
			auto toolingIndex = iterToolingIndex->second.value<quint64>();
			auto toolingData = iterData->second.value<QByteArray>();

			log(ML_LogLabel::NORMAL_LABEL, QString(u8"准备上传数据: 识别码类型[%1] 识别码[%2] 工装数据[%3]")
				.arg(QString::number(identifierType))
				.arg(QString(identifier))
				.arg(QString(toolingData)));

			//写执行状态
			auto watch = m_client->writeNodeVariable(MI_Device::s_deviceUploadWorkResultDataCommandName, MS_ExecuteState::EXECUTING, QOpcUa::Types::UInt16);
			QObject::connect(watch, &MC_FutureWatchBase::finished, this, [=]()
			{
				ME_DestructExecuter onDeleteObject([=]() {
					watch->deleteLater();
				});
				if (!watch->getIsSuccess())
				{
					log(ML_LogLabel::WARNING_LABEL, QString(u8"上传数据写完成状态失败 : %1").arg(watch->getErrorString()));
					onError();
					return;
				}
				log(ML_LogLabel::NORMAL_LABEL, u8"上传数据等待完成...");
				emit sig_deviceUploadData(MR_WorkToolingData(MI_ToolingIdentifier(identifier, identifierType), toolingIndex, toolingData));
				//等待上传
			});

		});
	});

	//写执行完成
	auto writeExecuteFinishState = new QState();
	QObject::connect(writeExecuteFinishState, &QState::entered, this, [=]()
	{
		//写数据有效
	/*	auto writeDataVaildWatch = m_client->writeNodeVariable(MI_Device::s_deviceUploadWorkResultDataToolingDataIsValidName, MS_DataValidState::NOT_VALID, QOpcUa::Types::UInt16);
		QObject::connect(writeDataVaildWatch, &MC_FutureWatchBase::finished, this, [=]()
		{
			writeDataVaildWatch->deleteLater();
			if (!writeDataVaildWatch->getIsSuccess())
			{
				qDebug() << "Write data valid state fail! " << writeDataVaildWatch->getErrorString();
				onError();
				return;
			}*/

			//重置上传数据指令为不执行
		auto writeNotRequireDataWatch = m_client->writeNodeVariable(MI_Device::s_deviceUploadWorkResultDataCommandName, MI_SendCommand::NOT_EXECUTE, QOpcUa::Types::UInt16);
		QObject::connect(writeNotRequireDataWatch, &MC_FutureWatchBase::finished, this, [=]()
		{
			ME_DestructExecuter onDeleteObject([=]() {
				writeNotRequireDataWatch->deleteLater();
			});
			if (!writeNotRequireDataWatch->getIsSuccess())
			{
				log(ML_LogLabel::WARNING_LABEL, QString(u8"上传数据指令置不执行失败 : %1").arg(writeNotRequireDataWatch->getErrorString()));
				onError();
				return;
			}

			setDeviceIsRequireUploadDataState(MS_DeviceReuireUploadDataState::NOT_REQUIRE);
			//写执行完成
			auto writeExecuteFinishedWatch = m_client->writeNodeVariable(MI_Device::s_deviceUploadWorkResultDataExecuteStateName, MS_ExecuteState::FINIHED, QOpcUa::Types::UInt16);
			QObject::connect(writeExecuteFinishedWatch, &MC_FutureWatchBase::finished, this, [=]()
			{
				ME_DestructExecuter onDeleteObject([=]() {
					writeExecuteFinishedWatch->deleteLater();
				});
				if (!writeExecuteFinishedWatch->getIsSuccess())
				{
					log(ML_LogLabel::WARNING_LABEL, QString(u8"上传数据指令写执行完成失败 : %1").arg(writeExecuteFinishedWatch->getErrorString()));
					onError();
					return;
				}
				curMachine->postEvent(new MT_TriggerEvent(MT_TriggerEventType::CommandExecuteFinished));
			});

		});

		//	});
	});


	auto onFailState = new QState();
	QObject::connect(onFailState, &QState::entered, this, [=]()
	{
		log(ML_LogLabel::WARNING_LABEL, u8"上传数据失败！");
		qDebug() << u8"Execute device upload data fail! ";
	});
	auto onSuccessState = new QState();
	QObject::connect(onSuccessState, &QState::entered, this, [=]()
	{
		log(ML_LogLabel::NORMAL_LABEL, u8"上传数据成功！");
		qDebug() << u8"Execute device upload data success! ";
	});

	///////////////////////

	readUploadCommandFirstTimeState->addTransition(this, &MC_GS600PDeviceControlBase::sig_hasReceiveDeviceRequireUploadDataCommand, checkDataValidState);

	auto checkDataValidSuccessTransition = new MT_Transition(MT_TriggerEventType::DataIsValid);
	checkDataValidSuccessTransition->setTargetState(startExecuteState);
	checkDataValidState->addTransition(checkDataValidSuccessTransition);

	auto checkDataValidFailTransition = new MT_Transition(MT_TriggerEventType::ExistError);
	checkDataValidFailTransition->setTargetState(onFailState);
	checkDataValidState->addTransition(checkDataValidFailTransition);

	///////////
	auto startExecuteSuccessTransition = new MT_Transition(MT_TriggerEventType::DataUploadFinished);
	startExecuteSuccessTransition->setTargetState(writeExecuteFinishState);
	startExecuteState->addTransition(startExecuteSuccessTransition);

	auto startExecuteFailTransition = new MT_Transition(MT_TriggerEventType::ExistError);
	startExecuteFailTransition->setTargetState(onFailState);
	startExecuteState->addTransition(startExecuteFailTransition);

	/////////////////
	auto writeExecuteFinishSuccessTransition = new MT_Transition(MT_TriggerEventType::CommandExecuteFinished);
	writeExecuteFinishSuccessTransition->setTargetState(onSuccessState);
	writeExecuteFinishState->addTransition(writeExecuteFinishSuccessTransition);

	auto writeExecuteFinishFailTransition = new MT_Transition(MT_TriggerEventType::ExistError);
	writeExecuteFinishFailTransition->setTargetState(onFailState);
	writeExecuteFinishState->addTransition(writeExecuteFinishFailTransition);

	//////////////
	onFailState->addTransition(readUploadCommandFirstTimeState);
	onSuccessState->addTransition(readUploadCommandFirstTimeState);


	curMachine->addState(readUploadCommandFirstTimeState);
	curMachine->setInitialState(readUploadCommandFirstTimeState);
	curMachine->addState(checkDataValidState);
	curMachine->addState(startExecuteState);
	curMachine->addState(writeExecuteFinishState);
	curMachine->addState(onFailState);
	curMachine->addState(onSuccessState);


}

void MC_GS600PDeviceControlBase::readDeviceIfUploadDataUntilSuccess()
{
	readVal(MI_Device::s_deviceUploadWorkResultDataCommandName,
		[=](ME_Error const & _error)
	{
		qDebug() << _error.getMessage();
		QTimer::singleShot(500, this, [=]()
		{
			this->readDeviceIfUploadDataUntilSuccess();
		});
	},
		[=](QVariant const & _val)
	{
		Q_UNUSED(_val);
		//nothing to do
	});
}

void MC_GS600PDeviceControlBase::readDeviceIfRequireDataUntilSuccess()
{
	readVal(MI_Device::s_deviceRequireDataCommandName,
		[=](ME_Error const & _error)
	{
		qDebug() << _error.getMessage();
		QTimer::singleShot(500, this, [=]()
		{
			this->readDeviceIfRequireDataUntilSuccess();
		});
	},
		[=](QVariant const & _val)
	{
		Q_UNUSED(_val)
			//nothing to do
	});
}

void MC_GS600PDeviceControlBase::setDeviceIfShowMainControlUiState(quint16 _val)
{

	if (getDeviceIfShowMainControlUiState() == _val)
	{
		return;
	}

	m_deviceIfShowMainControlUiState = _val;

	if (getIsMonitorShowMainUiFlag())
	{
		emit this->sig_deviceIfShowMainUiChanged(_val);
	}

}

void MC_GS600PDeviceControlBase::setDeviceWorkAreaIfHasToolingState(quint16 _val)
{
	if (getDeviceWorkAreaIfHasToolingState() == _val)
	{
		return;
	}
	m_deviceWorkAreaIfHasToolingState = _val;
	emit this->sig_deviceWorkAreaIfHasToolingStateChanged(_val);
}

void MC_GS600PDeviceControlBase::setDeviceWorkAreaWorkState(quint16 _val)
{

	if (getDeviceWorkAreaWorkState() == _val)
	{
		return;
	}
	if (_val == MS_DeviceWorkAreaWorkState::STATE_WORKING)
	{
		auto curDateTime = QDateTime::currentDateTime();
		setDeviceWorkAreaStartWorkDateTime(curDateTime);
		emit this->sig_deviceWorkAreaStartWorkDateTimeChanged(curDateTime);
	}
	m_deviceWorkAreaWorkState = _val;
	emit this->sig_deviceWorkAreaWorkStateChanged(_val);

}

void MC_GS600PDeviceControlBase::setPlanSendToolingStateRespond(quint16 _val)
{
	if (getPlanSendToolingStateRespond() == _val)
	{
		return;
	}
	m_planSendToolingStateRespond = _val;
	emit this->sig_planSendToolingRespondStateChanged(_val);
}

void MC_GS600PDeviceControlBase::setSendToolingCommandExecuteState(quint16 _val)
{
	if (getSendToolingCommandExecuteState() == _val)
	{
		return;
	}
	m_sendToolingCommandExecuteState = _val;
	emit this->sig_sendToolingCommandExecuteStateChanged(_val);
}

void MC_GS600PDeviceControlBase::setDeviceState(quint16 _val)
{
	if (getDeviceState() == _val)
	{
		return;
	}
	m_deviceState = _val;
	emit this->sig_deviceStateChanged(_val);
}

void MC_GS600PDeviceControlBase::makeNodeValueChangedConnection(const QString& _fieldName, std::function<void(quint16)> _onValChanedFun)
{

	auto node = m_client->getNode(_fieldName);
	if (node)
	{
		auto connection = QObject::connect(node, &QOpcUaNode::dataChangeOccurred, this, [=](QOpcUa::NodeAttribute _attribute, QVariant _val)
		{
			if (node) {}
			if (_attribute != QOpcUa::NodeAttribute::Value)
			{
				return;
			}
			if (_val.canConvert<quint16>())
			{
				_onValChanedFun(_val.value<quint16>());
			}
		});
		m_clientConnectedConnections.emplace_back(connection);

		QMetaObject::invokeMethod(m_client.get(), [=]() {
			m_client->addMonitorKeyWord(_fieldName);
		});

	}
}

void MC_GS600PDeviceControlBase::setInitCommandExecuteState(quint16 _val)
{

	if (getInitCommandExecuteState() == _val)
	{
		return;
	}
	m_initCommandExecuteState = _val;
	emit this->sig_initCommnandExecuteStateChanged(_val);

}

void MC_GS600PDeviceControlBase::setPlanReceiveToolingStateRespond(quint16 _val)
{
	if (getPlanReceiveToolingStateRespond() == _val)
	{
		return;
	}

	m_planReceiveToolingStateRespond = _val;
	emit this->sig_planReceiveToolingRespondStateChanged(_val);
}

void MC_GS600PDeviceControlBase::setReceiveToolingCommandExecuteState(quint16 _val)
{
	if (getReceiveToolingCommandExecuteState() == _val)
	{
		return;
	}

	m_receiveToolingCommandExecuteState = _val;
	emit this->sig_receiveToolingCommandExecuteStateChanged(_val);
}


void MC_GS600PDeviceControlBase::tryConnect(const QHostAddress& _serverIpAddress, quint16 _port)
{
	if (_serverIpAddress.isNull())
	{
		return;
	}
	m_client->createAndConnectServer(_serverIpAddress, _port);
}

void MC_GS600PDeviceControlBase::disconnectServer()
{
	clearConnectionMakeWhenConnection();
	stopExecuteRequireData();
	stopExecuteUploadData();
	m_client->disConnectServer();

}

MS_ConnectState MC_GS600PDeviceControlBase::getConnectState()
{
	if (!m_client)
	{
		return MS_ConnectState::DISCONNECTED;
	}
	switch (m_client->getConnectState())
	{
	case QOpcUaClient::ClientState::Disconnected:
		return MS_ConnectState::DISCONNECTED;
	case QOpcUaClient::ClientState::Closing:
		return MS_ConnectState::DISCONNECTING;
	case QOpcUaClient::ClientState::Connecting:
		return MS_ConnectState::CONNECTING;
	case QOpcUaClient::ClientState::Connected:
		return MS_ConnectState::CONNECTED;
	default:
		return MS_ConnectState::DISCONNECTED;
		break;
	}
}

void MC_GS600PDeviceControlBase::checkDeviceType()
{
	auto watch = m_client->readNodeVariable(MI_Device::s_deviceTypeString);
	QObject::connect(watch, &MC_FutureWatchBase::finished, this, [=]()
	{
		ME_DestructExecuter onDeleteObject([=]() {
			watch->deleteLater();
		});

		if (!watch->getIsSuccess())
		{
			emit this->sig_deviceTypeCheckResult(MM_Maybe<bool>(ME_Error(watch->getErrorString())));
			return;
		}

		if (!watch->getResult().canConvert<quint16>())
		{
			emit this->sig_deviceTypeCheckResult(MM_Maybe<bool>(ME_Error(u8"Read variable type is not right!")));
			return;
		}

		emit this->sig_deviceTypeCheckResult(MM_Maybe<bool>(watch->getResult().value<quint16>() == this->getCommunicationDeviceType()));
	});
}

void MC_GS600PDeviceControlBase::startExecuteCommand(
	const QString& _logHead,
	std::vector<std::pair<QString, std::function<bool(quint16)>>> const& _readChecks,
	const QString& _executeCommandField,
	std::function<void(const MM_MaybeOk&)> const & _resultFun,
	std::function<void()> _updateStateFun,
	const std::vector<std::pair<QString, std::pair<QOpcUa::Types, QVariant>>>& _valsWriteBeforeExecuteCommand,
	quint16 _executeCommandVal)

{
	Q_ASSERT(!_readChecks.empty());
	std::vector<QString> readFields;
	for (const auto& var : _readChecks)
	{
		readFields.emplace_back(var.first);
	}
	//在发送指令前检查
	auto readValsBeforeExecuteWatch = m_client->readMultiNodeVariables(readFields);
	QObject::connect(readValsBeforeExecuteWatch, &MC_FutureWatchBase::finished, this, [=]()
	{
		ME_DestructExecuter onDeleteObject([=]() {
			readValsBeforeExecuteWatch->deleteLater();
		});


		QString curExecuteStep = u8"[check state] ";
		if (!readValsBeforeExecuteWatch->getIsSuccess())
		{
			_resultFun(ME_Error(_logHead + curExecuteStep + u8"Fail to read node variable ！" + readValsBeforeExecuteWatch->getErrorString()));
			return;
		}
		auto variantResult = readValsBeforeExecuteWatch->getResult();
		for (const auto & var : variantResult)
		{
			if (!var.second.canConvert<quint16>())
			{
				_resultFun(ME_Error(_logHead + curExecuteStep + u8" Read node variable type is not right ！ - " + var.first));
				return;
			}
			auto aVal = var.second.value<quint16>();
			auto iter = std::find_if(_readChecks.begin(), _readChecks.end(), [=](const auto& _val)
			{
				return _val.first == var.first;
			});

			if (iter == _readChecks.end())
			{
				_resultFun(ME_Error(_logHead + curExecuteStep + u8" Read field does not find ！"));
				return;
			}

			if (!iter->second(aVal))
			{
				_resultFun(ME_Error(_logHead + curExecuteStep + iter->first + u8" = " + QString::number(aVal)));
				return;
			}

		}


		auto sendExecuteFun = [=]() {
			// 发送执行指令
			auto executeWatch = m_client->writeNodeVariable(_executeCommandField, _executeCommandVal, QOpcUa::Types::UInt16);
			QObject::connect(executeWatch, &MC_FutureWatchBase::finished, this, [=]()
			{
				ME_DestructExecuter onDeleteObject([=]() {
					executeWatch->deleteLater();
				});


				QString curExecuteStep = u8"[send execute command] ";
				if (!executeWatch->getIsSuccess())
				{
					_resultFun(ME_Error(_logHead + curExecuteStep + u8" Fail to send command ！" + executeWatch->getErrorString()));
					return;
				}

				_resultFun(MM_MaybeOk());
			});
		};

		if (_valsWriteBeforeExecuteCommand.empty()) {
			sendExecuteFun();
		}
		else {
			//执行前置值
			auto setValsStateWath = m_client->writeMultiNodeVariables(_valsWriteBeforeExecuteCommand);
			QObject::connect(setValsStateWath, &MC_FutureWatchBase::finished, this, [=]()
			{
				ME_DestructExecuter onDeleteObject([=]() {
					setValsStateWath->deleteLater();
				});


				QString curExecuteStep = u8"[set values before send execute command state] ";
				if (!setValsStateWath->getIsSuccess())
				{
					_resultFun(ME_Error(_logHead + curExecuteStep + u8" Fail to write field ！" + setValsStateWath->getErrorString()));
					return;
				}

				_updateStateFun();

				sendExecuteFun();
			});
		}
	});
}

void MC_GS600PDeviceControlBase::startExecuteCommand(
	const QString& _logHead,
	const QString& _executeStateField,
	const QString& _executeCommandField,
	std::function<void(const MM_MaybeOk&)>  _resultFun,
	std::function<void()> _updateStateFun)
{

	std::vector<std::pair<QString, std::function<bool(quint16)>>> readChecks;
	readChecks.emplace_back(std::make_pair(_executeStateField, [=](quint16 _state)
	{
		if (_state == MS_ExecuteState::NOT_EXECUTE || _state == MS_ExecuteState::FINIHED)
		{
			return true;
		}
		else
		{
			return false;
		}
	}));
	std::vector<std::pair<QString, std::pair<QOpcUa::Types, QVariant>>> writeBeforeSend;
	writeBeforeSend.emplace_back(std::make_pair(_executeStateField, std::make_pair(QOpcUa::Types::UInt16, MS_ExecuteState::NOT_EXECUTE)));

	startExecuteCommand(_logHead,
		readChecks,
		_executeCommandField,
		_resultFun,
		_updateStateFun,
		writeBeforeSend);
}

void MC_GS600PDeviceControlBase::readMultiVal(std::vector<QString> const& _keyNames, std::function<void(MP_Public::ME_Error const & _error)> const & _onFail, std::function<void(std::map<QString, QVariant> const & _val)> const & _onSuccess)
{
	auto watch = m_client->readMultiNodeVariables(_keyNames);
	QObject::connect(watch, &MC_FutureWatchBase::finished, this, [=]()
	{
		ME_DestructExecuter onDeleteObject([=]() {
			watch->deleteLater();
		});


		if (!watch->getIsSuccess())
		{
			_onFail(ME_Error(watch->getErrorString()));
			return;
		}

		_onSuccess(watch->getResult());
	});
}

QString MC_GS600PDeviceControlBase::getTransitionKeyString(ME_TransitionKeyWordType _type)
{
	auto iter = m_transitionKeyWordMap.find(_type);
	if (iter == m_transitionKeyWordMap.end()) {
		return QString();
	}
	return iter->second;
}

void MC_GS600PDeviceControlBase::executeInitCommand()
{
	startExecuteCommand(u8"send init command: ",
		MI_Device::s_initCommandExecuteStateName,
		MI_Device::s_initCommandSendName,
		[=](MM_MaybeOk const & _val)
	{
		if (_val.hasError()) {
			log(ML_LogLabel::WARNING_LABEL, _val.getError()->getMessage());
		}

		emit sig_executeInitCommandResult(_val);
	}, [=]() {
		setInitCommandExecuteState(MS_ExecuteState::NOT_EXECUTE);
	}
	);
}

void MC_GS600PDeviceControlBase::readVal(QString const& _fieldName,
	std::function<void(ME_Error const & _error)> const & _onFail,
	std::function<void(QVariant const & _val)> const & _onSuccess)
{
	auto watch = m_client->readNodeVariable(_fieldName);
	QObject::connect(watch, &MC_FutureWatchBase::finished, this, [=]()
	{
		ME_DestructExecuter onDeleteObject([=]() {
			watch->deleteLater();
		});

		if (!watch->getIsSuccess())
		{
			_onFail(ME_Error(watch->getErrorString()));
			return;
		}

		if (!watch->getResult().canConvert<quint16>())
		{
			_onFail(ME_Error(u8"Read variable type is not right!"));
			return;
		}

		_onSuccess(watch->getResult());
	});
}

void MC_GS600PDeviceControlBase::readReadyToReceiveToolingState()
{
	readVal(MI_Device::s_isReadyToReceiveToolingStateName,
		[=](ME_Error const & _error)
	{
		emit this->sig_readReadyToReceiveToolingStateResult(_error);
	},
		[=](QVariant const & _val)
	{
		emit this->sig_readReadyToReceiveToolingStateResult(MM_Maybe<bool>(_val.value<quint16>() == MS_IsReadyReceiveAndSendToolingState::HAS_READY));
	});
}


void MC_GS600PDeviceControlBase::readReadyToSendToolingState()
{
	readVal(MI_Device::s_isReadyToSendToolingStateName,
		[=](ME_Error const & _error)
	{
		emit this->sig_readReadyToSendToolingStateResult(_error);
	},
		[=](QVariant const & _val)
	{
		emit this->sig_readReadyToSendToolingStateResult(MM_Maybe<bool>(_val.value<quint16>() == MS_IsReadyReceiveAndSendToolingState::HAS_READY));
	});
}




void MC_GS600PDeviceControlBase::executePlanNode(const QString& _planRespondFieldName,
	const QString& _beInPlanFieldName,
	std::function<void(MP_Public::ME_Error const & _val)> _onError,
	std::function<void()> const & _onSuccess
)
{
	//置规划状态应答初值
	auto setPlanRespondWatch = m_client->writeNodeVariable(_planRespondFieldName, MI_PlanRespond::INIT_VALUE, QOpcUa::Types::UInt16);
	QObject::connect(setPlanRespondWatch, &MC_FutureWatchBase::finished, this, [=]()
	{
		ME_DestructExecuter onDeleteObject([=]() {
			setPlanRespondWatch->deleteLater();
		});

		if (!setPlanRespondWatch->getIsSuccess())
		{
			_onError(ME_Error(u8"Set plan respond fail！"));
			return;
		}


		//置规划状态
		auto setPlanStateWatch = m_client->writeNodeVariable(_beInPlanFieldName, MI_PlanState::BE_IN_PLANNING, QOpcUa::Types::UInt16);
		QObject::connect(setPlanStateWatch, &MC_FutureWatchBase::finished, this, [=]()
		{
			ME_DestructExecuter onDeleteObject([=]() {
				setPlanStateWatch->deleteLater();
			});

			if (!setPlanStateWatch->getIsSuccess())
			{
				_onError(ME_Error(u8"Set plan state fail!"));
				return;
			}
			_onSuccess();
		});
	});
}

void MC_GS600PDeviceControlBase::planReceiveTooling()
{

	executePlanNode(MI_Device::s_receiveToolingBeInPlanningRespondName,
		MI_Device::s_receiveToolingBeInPlanningStateName,
		[=](ME_Error const _val)
	{
		emit sig_startExecutePlanReceiveToolingResult(_val);
	}, [=]()
	{
		emit sig_startExecutePlanReceiveToolingResult(MM_MaybeOk());
	});
}

void MC_GS600PDeviceControlBase::planSendTooling()
{
	executePlanNode(MI_Device::s_sendToolingBeInPlanningRespondName,
		MI_Device::s_sendToolingBeInPlanningStateName,
		[=](ME_Error const& _val)
	{
		emit sig_startExecutePlanSendToolingResult(_val);
	}, [=]()
	{
		emit sig_startExecutePlanSendToolingResult(MM_MaybeOk());
	});
}

void MC_GS600PDeviceControlBase::executeSendToolingCommand()
{
	startExecuteCommand(u8"Send send tooling command: ",
		MI_Device::s_sendToolingCommandExecuteStateName,
		MI_Device::s_sendToolingCommandName,
		[=](MM_MaybeOk const & _val)
	{
		emit sig_startExecuteSendToolingCommandResult(_val);
	},
		[=]() {
		setSendToolingCommandExecuteState(MS_ExecuteState::NOT_EXECUTE);
	}
	);
}

void MC_GS600PDeviceControlBase::cancelPlanSendTooling()
{
	cancelPlan(MI_Device::s_sendToolingBeInPlanningRespondName,
		MI_Device::s_sendToolingBeInPlanningStateName,
		[this](const ME_Error& _val)
	{
		emit this->sig_cancelPlanSendToolingResult(_val);
	},
		[this]()
	{
		emit this->sig_cancelPlanSendToolingResult(MM_MaybeOk());
	});

}

void MC_GS600PDeviceControlBase::readDeviceIfConfigMainControl()
{
	readVal(MI_Device::s_deviceIfConfigMainControl,
		[=](ME_Error const & _error)
	{
		emit this->sig_readDeviceIfConfigMainControlResult(_error);
	},
		[=](QVariant const & _val)
	{
		emit this->sig_readDeviceIfConfigMainControlResult(MM_Maybe<bool>(_val.value<quint16>() == MS_DeviceIfConfigMainControl::IS_CONFIG));
	});
}

void MC_GS600PDeviceControlBase::onSendToolingData(const MR_WorkToolingData& _data, bool _isDataValid, bool _isDoAll)
{
	std::vector<std::pair<QString, std::pair<QOpcUa::Types, QVariant>>> dataToWrite;
	dataToWrite.emplace_back(std::make_pair(MI_Device::s_deviceRequireDataToolingIndexName, std::make_pair(QOpcUa::Types::UInt64, _data.m_toolingIndex)));
	if (!_isDoAll)
	{
		dataToWrite.emplace_back(std::make_pair(MI_Device::s_deviceRequireDataToolingDataContentName, std::make_pair(QOpcUa::Types::ByteString, _data.m_data)));

	}
	dataToWrite.emplace_back(std::make_pair(MI_Device::s_deviceRequireDataToolingIfDoAllName, std::make_pair(QOpcUa::Types::UInt16, (_isDoAll ? MS_IfWorkToolingDoAllFlag::DO_ALL : MS_IfWorkToolingDoAllFlag::NOT_DO_ALL))));
	dataToWrite.emplace_back(std::make_pair(MI_Device::s_deviceRequireDataToolingDataIsValidName, std::make_pair(QOpcUa::Types::UInt16, (_isDataValid ? MS_DataValidState::IS_VALID : MS_DataValidState::NOT_VALID))));

	if (_isDoAll) {
		log(ML_LogLabel::NORMAL_LABEL, QString(u8"准备下发数据:全做"));
	}
	else
	{
		if (!_isDataValid) {
			log(ML_LogLabel::NORMAL_LABEL, QString(u8"准备下发数据:数据无效"));
		}
		else
		{
			log(ML_LogLabel::NORMAL_LABEL, QString(u8"准备下发数据:%1").arg(QString(_data.m_data)));
		}
	}


	auto watch = m_client->writeMultiNodeVariables(dataToWrite);
	QObject::connect(watch, &MC_FutureWatchBase::finished, this, [=]()
	{

		ME_DestructExecuter onDeleteObject([=]() {
			watch->deleteLater();
		});

		if (!watch->getIsSuccess())
		{
			this->m_onClientRequireDataMachine->postEvent(new MT_TriggerEvent(MT_TriggerEventType::ExistError));
		}
		else
		{
			this->m_onClientRequireDataMachine->postEvent(new MT_TriggerEvent(MT_TriggerEventType::CommandWriteDataSuccess));
		}
	});
}

void MC_GS600PDeviceControlBase::startWaitExecuteRequireData()
{
	if (m_onClientRequireDataMachine->isRunning())
	{
		return;
	}
	m_onClientRequireDataMachine->start();
}

void MC_GS600PDeviceControlBase::stopExecuteRequireData()
{
	m_onClientRequireDataMachine->stop();
}

void MC_GS600PDeviceControlBase::startWaitUploadData()
{
	if (m_onClientUploadDataMachine->isRunning())
	{
		return;
	}
	m_onClientUploadDataMachine->start();
}

void MC_GS600PDeviceControlBase::stopExecuteUploadData()
{
	m_onClientUploadDataMachine->stop();
}

void MC_GS600PDeviceControlBase::executeReceiveToolingCommand()
{
	startExecuteCommand(u8"Send receive tooling command: ",
		MI_Device::s_receiveToolingCommandExecuteStateName,
		MI_Device::s_receiveToolingCommandName,
		[=](MM_MaybeOk const & _val)
	{
		emit sig_startExecuteReceiveToolingCommandResult(_val);
	},
		[=]() {
		setReceiveToolingCommandExecuteState(MS_ExecuteState::NOT_EXECUTE);
	}
	);
}


void MC_GS600PDeviceControlBase::cancelPlan(const QString& _planStateFieldName,
	const QString& _planCommandFieldName,
	std::function<void(MP_Public::ME_Error const & _val)> _onError,
	std::function<void()> _onSuccess)
{
	using ReturnType = std::vector<std::pair<QString, std::pair<QOpcUa::Types, QVariant>>>;
	ReturnType	dataToWrite;
	dataToWrite.emplace_back(std::make_pair(_planStateFieldName, std::make_pair(QOpcUa::Types::UInt16, MI_PlanRespond::INIT_VALUE)));
	dataToWrite.emplace_back(std::make_pair(_planCommandFieldName, std::make_pair(QOpcUa::Types::UInt16, MI_PlanState::NOT_BE_IN_PLANNING)));
	writeMultiVals(dataToWrite, _onError, _onSuccess);
}

void MC_GS600PDeviceControlBase::writeSingleVal(
	const QString& _fieldName,
	const QVariant& _val,
	QOpcUa::Types _valtype,
	std::function<void(MP_Public::ME_Error const & _val)> _onError,
	std::function<void()> _onSuccess)
{
	auto watch = m_client->writeNodeVariable(_fieldName, _val, _valtype);
	QObject::connect(watch, &MC_FutureWatchBase::finished, this, [=]()
	{
		ME_DestructExecuter onDeleteObject([=]() {
			watch->deleteLater();
		});

		if (!watch->getIsSuccess())
		{
			_onError(ME_Error(watch->getErrorString()));
			return;
		}
		_onSuccess();
	});
}

void MC_GS600PDeviceControlBase::writeMultiVals(std::vector<std::pair<QString, std::pair<QOpcUa::Types, QVariant>>> const& _vals, std::function<void(MP_Public::ME_Error const & _val)> _onError, std::function<void()> _onSuccess)
{
	auto watch = m_client->writeMultiNodeVariables(_vals);
	QObject::connect(watch, &MC_FutureWatchBase::finished, this, [=]()
	{
		ME_DestructExecuter onDeleteObject([=]() {
			watch->deleteLater();
		});

		if (!watch->getIsSuccess())
		{
			_onError(ME_Error(watch->getErrorString()));
			return;
		}
		_onSuccess();
	});
}

QDateTime MC_GS600PDeviceControlBase::getDeviceReadyToReceiveInDateTime()
{
	QMutexLocker locker(&m_deviceReadyToReceiveInDateTimeMutex);
	return m_deviceReadyToReceiveInDateTime;
}

void MC_GS600PDeviceControlBase::setDeviceReadyToReceiveInDateTime(const QDateTime& val)
{
	QMutexLocker locker(&m_deviceReadyToReceiveInDateTimeMutex);
	m_deviceReadyToReceiveInDateTime = val;
}

QDateTime MC_GS600PDeviceControlBase::getDeviceReadyToSendOutDateTime()
{
	QMutexLocker locker(&m_deviceReadyToSendOutDateTimeMutex);
	return m_deviceReadyToSendOutDateTime;
}

void MC_GS600PDeviceControlBase::setDeviceReadyToSendOutDateTime(const QDateTime& val)
{
	QMutexLocker locker(&m_deviceReadyToSendOutDateTimeMutex);
	m_deviceReadyToSendOutDateTime = val;
}

void MC_GS600PDeviceControlBase::cancelPlanReceviceTooling()
{
	cancelPlan(
		MI_Device::s_receiveToolingBeInPlanningRespondName,
		MI_Device::s_receiveToolingBeInPlanningStateName,
		[this](const ME_Error& _val)
	{
		emit this->sig_cancelPlanReceiveToolingResult(_val);
	},
		[this]()
	{
		emit this->sig_cancelPlanReceiveToolingResult(MM_MaybeOk());
	});
}

void MC_GS600PDeviceControlBase::clearConnectionMakeWhenConnection()
{
	for (auto& var : m_clientConnectedConnections)
	{
		QObject::disconnect(var);
	}
	m_clientConnectedConnections.clear();
	m_clientConnectedConnections.shrink_to_fit();
}



