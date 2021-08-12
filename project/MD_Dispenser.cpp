#include "MD_Dispenser.h"
#include "MC_OpcDeviceControl.h"
#include "MT_Transition.h"
#include "MA_ThreadAuxiliary.h"
#include "ML_GlobalLog.h"
#include <QFinalState>
#include <QTimer>
#include <QThread>

#define MD_DISPENSER_LOG_NAME "DispenserStation"

using namespace StateMachine;
using namespace MP_Public;

MD_Dispenser::MD_Dispenser(const QString& _name, QObject* _parent /*= nullptr*/)
	:MI_DeviceInterface(_name, _parent),
	MS_StateMachineAuxiliary(),
	m_name(_name),
	m_runMachine(new MS_StateMachine(this)),
	m_runInManualMachine(new MS_StateMachine(this)),
	m_pollingPlanResultTimer(new QTimer(this)),
	m_pollingExecuteCommandTimer(new QTimer(this)),
	m_pollingInitCommandFinishTimer(new QTimer(this)),
	m_controlThread(new QThread()),
	m_pollingConnectSuccessTimer(new QTimer(this))
{
	auto object = Auxiliary::syncStartThread(m_controlThread.get());
	Auxiliary::blockSyncExecute(object.get(), [=]() {
		m_control = std::make_shared<MC_OpcDeviceControl>(_name);
	});

	QObject::connect(getControl(), &ML_LogBase::sig_log, this, [=](const auto& _name, ML_LogLabel _label, const QString& _logInfo) {
		log(_label, _logInfo);
	});

	QObject::connect(getControl(), &MC_OpcDeviceControl::sig_connectStateChanged, this, [=](auto _result) {
		if (_result == MS_ConnectState::CONNECTED) {
			emit sig_deviceConnected(MP_Public::MM_MaybeOk());
		}
		else if (_result == MS_ConnectState::DISCONNECTED) {
			emit sig_deviceDisconnected();
		}
	});

	m_pollingPlanResultTimer->callOnTimeout(this, [=]() {
		Q_ASSERT(getControl());
		auto respond = getControl()->getPlanReceiveAndSendWaferStateRespond();
		if (respond == MI_PlanRespond::INIT_VALUE) {
			return;
		}
		else if (respond == MI_PlanRespond::ALLOWED_PLAN) {
			postSignalEvent(m_runMachine, this, &MD_Dispenser::sig_allowPlaned);
			postSignalEvent(m_runInManualMachine, this, &MD_Dispenser::sig_allowPlaned);
			emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"允许规划...");
		}
		else if (respond == MI_PlanRespond::NOT_ALLOWED_PLAN) {
			postSignalEvent(m_runMachine, this, &MD_Dispenser::sig_notAllowPlaned);
			postSignalEvent(m_runInManualMachine, this, &MD_Dispenser::sig_notAllowPlaned);
			emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"不允许规划...");
		}
	});

	m_pollingExecuteCommandTimer->callOnTimeout(this, [=]() {
		Q_ASSERT(getControl());
		auto executeCommnadVal = getControl()->getReceiveAndSendWaferCommand();
		if (executeCommnadVal == MI_SendCommand::NOT_EXECUTE) {
			return;
		}
		else if (executeCommnadVal == MI_SendCommand::NEED_EXECUTE) {
			postSignalEvent(m_runMachine, this, &MD_Dispenser::sig_requiredExecuteReceiveAndSendWafer);
			postSignalEvent(m_runInManualMachine, this, &MD_Dispenser::sig_requiredExecuteReceiveAndSendWafer);
		}
	});

	m_pollingInitCommandFinishTimer->callOnTimeout(this, [=]() {
		if (getControl()->getInitCommandExecuteState() == MS_ExecuteState::FINIHED) {
			emit sig_deviceResetFinished(MM_MaybeOk());
			emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"复位完成！");
			m_pollingInitCommandFinishTimer->stop();
		}
		else if (getControl()->getInitCommandExecuteState() == MS_ExecuteState::ERROR_EXECUTING) {
			emit sig_deviceResetFinished(ME_Error({ u8"复位失败！" }));
			emit sig_logInfo(ML_LogLabel::ERROR_LABEL, u8"复位失败！");
			m_pollingInitCommandFinishTimer->stop();
		}
	});

	m_pollingConnectSuccessTimer->callOnTimeout(this, [=]() {
		auto control{ getControl() };
		Q_ASSERT(control);
		if (control->getConnectState() == MS_ConnectState::CONNECTED) {
			m_pollingConnectSuccessTimer->stop();
			emit sig_tryConnectedSuccess();
			emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"重新连接设备成功...");
		}
	});

	initRunMachine();
	this->initRunMachineInManual();
	QObject::connect(m_runMachine, &QStateMachine::stopped, this, [=]() {
		setIsExecutingRunProcess(false);
	});
}

MD_Dispenser::~MD_Dispenser()
{
	getControl()->disconnectServer();
	m_controlThread->quit();
	m_controlThread->wait(5000);
}

MP_Public::MM_MaybeOk MD_Dispenser::initDevice()
{
	return{};
}

bool MD_Dispenser::tryConnectDevice()
{
	Q_ASSERT(getControl());
	QMetaObject::invokeMethod(getControl(), [=]()
	{
		log(ML_LogLabel::NORMAL_LABEL, QString(u8"开始连接[%1:%2]...").arg(getHostAddress().toString()).arg(getPort()));
		getControl()->tryConnect(this->getHostAddress(), this->getPort());

		emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"连接成功！");
	});
	return true;
}

bool MD_Dispenser::isDeviceConnected()
{
	Q_ASSERT(m_control);
	return getControl()->getConnectState() == MS_ConnectState::CONNECTED;
}

bool MD_Dispenser::disconnectDevice()
{
	Q_ASSERT(getControl());
	QMetaObject::invokeMethod(getControl(), [=]()
	{
		getControl()->disconnectServer();
	});
	return true;
}

void MD_Dispenser::resetDevice()
{
	m_pollingInitCommandFinishTimer->stop();
	Q_ASSERT(getControl());
	auto object(new QObject());
	QObject::connect(getControl(), &MC_OpcDeviceControl::sig_executeInitCommandResult, object, [=](const auto& _result) {
		auto executeOnEnd = std::shared_ptr<void>(nullptr, [=](auto _p) {
			getControl()->disconnect(object);
			object->deleteLater();
		});
		if (_result.hasError()) {
			QTimer::singleShot(0, this, [=]() {
				emit sig_deviceResetFinished(*_result.getError());
				emit sig_logInfo(ML_LogLabel::ERROR_LABEL, u8"复位失败! " + _result.getError()->getMessage());
			});
		}
		else {
			m_pollingInitCommandFinishTimer->start(500);
		}
	});
	QMetaObject::invokeMethod(getControl(), [=]()
	{
		getControl()->executeInitCommand();
		emit this->sig_executeOperateStateChanged(u8"执行初始化");
	});
}

void MD_Dispenser::inquireIfHasWaferToTakeOut()
{
	Q_ASSERT(getControl());
	emit sig_inquireIfHasWafeResult(MP_Public::MM_Maybe<bool>(this->getControl()->getDeviceWorkAreaIfHasWaferState() == MS_DeviceWorkAreaIfHasWaferState::STATE_HAS));
}

double MD_Dispenser::getTransportPosition() const
{
	return getParam().m_transportStationPosition;
}

bool MD_Dispenser::pauseDevice()
{
	m_runMachine->suspend();
	return true;
}

bool MD_Dispenser::resumeRunDevice()
{
	m_runMachine->resume();
	return true;
}

bool MD_Dispenser::stopDevice()
{
	if (m_runMachine->isSuspending()) {
		m_runMachine->resume();
	}
	emit sig_requireStop();
	return true;
}

bool MD_Dispenser::startExecuteProcess()
{
	if (m_runMachine->isSuspending()) {
		m_runMachine->resume();
	}
	if (!m_runMachine->isRunning()) {
		m_runMachine->start();
	}
	setIsExecutingRunProcess(true);
	return true;
}

bool MD_Dispenser::startExecuteManualProcess()
{
	/*if (m_runInManualMachine->isSuspending()) {
		m_runInManualMachine->resume();
	}*/
	if (!m_runInManualMachine->isRunning()) {
		m_runInManualMachine->start();
	}
	setIsExecutingRunProcess(true);
	return true;
}

void MD_Dispenser::stopManualStateMachine()
{
	emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"点胶工位:停止半自动运行逻辑");
	emit sig_requireStopManualStateMachine();
}


bool MD_Dispenser::isReadyTakenOutWafer()
{
	return checkIfReadyTakeOut() && m_isInWaitState;
}
bool MD_Dispenser::isReadyTakenOutWaferInManual()
{
	return this->checkIfReadyTakeOutInManual() && m_isInWaitState;
}

bool MD_Dispenser::checkIfReadyTakeOut()
{
	return isDeviceConnected()
		&& (getControl()->getDeviceWorkAreaIfHasWaferState() == MS_DeviceWorkAreaIfHasWaferState::STATE_HAS)
		&& (getControl()->getDeviceReadyToReceiveAndSendWaferState() == MS_IsReadyReceiveAndSendWaferState::HAS_READY_SEND);
}

bool MD_Dispenser::checkIfReadyTakeOutInManual()
{
	return isDeviceConnected()
		&& (getControl()->getDeviceReadyToReceiveAndSendWaferState() == MS_IsReadyReceiveAndSendWaferState::HAS_READY_SEND);
}

bool MD_Dispenser::isReadyPutInWafer(MW_Wafer* _wafer)
{
	if (!getPreSelectWafer())
	{
		return checkIfReadyToPutIn();
	}
	return checkIfReadyToPutIn() && getPreSelectWafer().get() == _wafer;
}

bool MD_Dispenser::isReadyPutInWaferInManual()
{
	return checkIfReadyToPutInInManual();
}

MN_PC100::MI_DeviceInteractionStatus::MT_DeviceIsReady MD_Dispenser::isReadyPutInOrTakeOutWafer()
{
	if (checkIfReadyTakeOut() && m_isInWaitState) {
		return MN_PC100::MI_DeviceInteractionStatus::MT_DeviceIsReady::READY_TAKEOUT;
	}
	if (checkIfReadyToPutIn()) {
		return MN_PC100::MI_DeviceInteractionStatus::MT_DeviceIsReady::READY_PUTIN;
	}
	return MN_PC100::MI_DeviceInteractionStatus::MT_DeviceIsReady::NOT_READY;
}

bool MD_Dispenser::checkIfReadyToPutIn()
{
	return  isDeviceConnected()
		&& (getControl()->getDeviceWorkAreaIfHasWaferState() == MS_DeviceWorkAreaIfHasWaferState::STATE_NONE)
		&& (getControl()->getDeviceReadyToReceiveAndSendWaferState() == MS_IsReadyReceiveAndSendWaferState::HAS_READY_RECEIVE);
}

bool MD_Dispenser::checkIfReadyToPutInInManual()
{
	return  isDeviceConnected()
		&& (getControl()->getDeviceReadyToReceiveAndSendWaferState() == MS_IsReadyReceiveAndSendWaferState::HAS_READY_RECEIVE);
}

void MD_Dispenser::onFinishPutInWafer()
{
	emit sig_receiveAndSendWaferSuccessFinish();
}

void MD_Dispenser::onFinishTakeOutWafer()
{
	emit sig_receiveAndSendWaferSuccessFinish();
}

void MD_Dispenser::requireDeviceConfirmCanPutInWaferToDeviceAtOnce(std::shared_ptr<MW_Wafer> _wafer)
{
	if (checkIfReadyToPutIn()) {
		m_isToTakeOutWafer = false;
		QTimer::singleShot(0, this, [=]() {
			emit sig_goStartPlanning();
		});
	}
	else {
		QTimer::singleShot(0, this, [=]() {
			emit sig_deviceConfirmCanPutInToIt(MP_Public::MM_Maybe<bool>(false));
		});
	}
}

void MD_Dispenser::requireDeviceConfirmCanPutInWaferToDeviceAtOnceInManual()
{
	if (checkIfReadyToPutInInManual()) {
		m_isToTakeOutWafer = false;
		QTimer::singleShot(0, this, [=]() {
			emit sig_goStartPlanning();
		});
	}
	else {
		QTimer::singleShot(0, this, [=]() {
			emit sig_deviceConfirmCanPutInToIt(MP_Public::MM_Maybe<bool>(false));
		});
	}
}


void MD_Dispenser::requireDeviceConfirmCanTakeOutWaferFromDeviceAtOnce()
{
	if (checkIfReadyTakeOut()) {
		m_isToTakeOutWafer = true;
		QTimer::singleShot(0, this, [=]() {
			emit sig_goStartPlanning();
		});
	}
	else {
		QTimer::singleShot(0, this, [=]() {
			emit sig_deviceConfirmCanTakeOutFromIt(MP_Public::MM_Maybe<bool>(false));
		});
	}
}

void MD_Dispenser::requireDeviceConfirmCanTakeOutWaferFromDeviceAtOnceInManual()
{
	if (checkIfReadyTakeOutInManual()) {
		m_isToTakeOutWafer = true;
		QTimer::singleShot(0, this, [=]() {
			emit sig_goStartPlanning();
		});
	}
	else {
		QTimer::singleShot(0, this, [=]() {
			emit sig_deviceConfirmCanTakeOutFromIt(MP_Public::MM_Maybe<bool>(false));
		});
	}
}

void MD_Dispenser::unlockForTransportWafer()
{
	QMetaObject::invokeMethod(getControl(), [=]()
	{
		getControl()->writeSingleVal(
			MI_Device::s_deviceBeLockedName,
			MS_DeviceBeLockedState::Not_Be_Locked,
			QOpcUa::Types::UInt16,
			[=](const MP_Public::ME_Error& _error)
		{
			emit this->sig_errorInfo(_error);
		},
			[=]()
		{
			auto gard{this->lockgarderForLockTransportWafer()};
			m_isLockForTransportWafer = false;
		});
	});
}

void MD_Dispenser::updateDevicesInformationInHomePage(MN_PC100::MI_DeviceInteractionStatus & _var)
{
	emit sig_updateDevicesInformationInHomePage(_var);
}

QHostAddress MD_Dispenser::getHostAddress() const
{
	return getParam().m_ipAddress;
}

quint16 MD_Dispenser::getPort() const
{
	return getParam().m_port;
}

void MD_Dispenser::writeLogFileForParam(const QString & _var)
{
	emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, _var);
}


void MD_Dispenser::log(ML_LogLabel _label, const QString& _logInfo)
{
	emit this->sig_logInfo(_label, _logInfo);
}

void MD_Dispenser::initRunMachine()
{
	auto curMachine = m_runMachine;

	auto topState = new QState();


	auto m_executePutInOrTakeOutWaferTopState = new QState(topState);
	QObject::connect(m_executePutInOrTakeOutWaferTopState, &QState::entered, this, [=]() {
		emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"进入执行取放wafer主状态！");
		QTimer::singleShot(0, this, [=]() {
			m_isInWaitState = true;;
		});
	});
	QObject::connect(m_executePutInOrTakeOutWaferTopState, &QState::exited, this, [=]() {
		m_isInWaitState = false;
	});
	//等待
	auto waitToExecuteState = new QState(m_executePutInOrTakeOutWaferTopState);
	QObject::connect(waitToExecuteState, &QState::entered, this, [=]() {
		m_isToTakeOutWafer = {};
		emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"等待执行...");
	});

	//规划
	auto planState = new QState(m_executePutInOrTakeOutWaferTopState);
	QObject::connect(planState, &QState::entered, this, [=]() {
		emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"开始规划...");
		auto object = new QObject();
		setStateOnExitAction(curMachine, planState, [=]() {
			getControl()->disconnect(object);
			object->deleteLater();

			m_pollingPlanResultTimer->stop();
		});
		QObject::connect(getControl(), &MC_OpcDeviceControl::sig_startExecutePlanReceiveSendWaferResult, object, [=](const auto& _result) {
			if (_result.hasError()) {
				emit sig_errorInfo({ u8"Start execute plan fail!" });
				emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"规划失败...");
				return;
			}
			m_pollingPlanResultTimer->start(300);
		});
		QMetaObject::invokeMethod(getControl(), [=]() {
			getControl()->planReceiveSendWafer();
		});
	});

	QObject::connect(planState, &QState::exited, this, [=]() {
		executeStateOnExitAction(curMachine, planState);
	});

	//等待执行指令
	auto waitExecuteCommandState = new QState(m_executePutInOrTakeOutWaferTopState);
	QObject::connect(waitExecuteCommandState, &QState::entered, this, [=]() {
		emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"等待执行指令...");
		setStateOnExitAction(curMachine, waitExecuteCommandState, [=]() {
			m_pollingExecuteCommandTimer->stop();
		});
		m_pollingExecuteCommandTimer->start(100);
	});

	QObject::connect(waitExecuteCommandState, &QState::exited, this, [=]() {
		executeStateOnExitAction(curMachine, waitExecuteCommandState);
	});

	//置锁定状态
	auto setLockedState = new QState(m_executePutInOrTakeOutWaferTopState);
	QObject::connect(setLockedState, &QState::entered, this, [=]() {
		emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"锁定设备");
		QMetaObject::invokeMethod(getControl(), [=]() {
			getControl()->writeSingleVal(
				MI_Device::s_deviceBeLockedName,
				MS_DeviceBeLockedState::Be_Loked,
				QOpcUa::Types::UInt16,
				[=](const MP_Public::ME_Error& _error) {
				emit this->sig_errorInfo(_error); },
				[=]() {
					postSignalEvent(curMachine, this, &MD_Dispenser::sig_lockDeviceSuccess);
				});
		});
	});

	//开始执行收送wafer
	auto startExecuteReceiveAndSendWaferState = new QState(topState);
	QObject::connect(startExecuteReceiveAndSendWaferState, &QState::entered, this, [=]() {
		emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"置执行中状态...");
		QMetaObject::invokeMethod(getControl(), [=]() {
			getControl()->writeSingleVal(
				MI_Device::s_deviceReceivceSendWaferCommandExecuteStateKeyName,
				MS_ExecuteState::EXECUTING,
				QOpcUa::Types::UInt16,
				[=](const MP_Public::ME_Error& _error) {
				emit this->sig_errorInfo(_error); },
				[=]() {
					postSignalEvent(curMachine, this, &MD_Dispenser::sig_goWaitExecuteFinished);
				});
		});
	});

	//等待结束
	auto waitExeuteFinishedState = new QState(topState);
	QObject::connect(waitExeuteFinishedState, &QState::entered, this, [=]() {
		m_isWaitActionExecute = true;
		QTimer::singleShot(0, this, [=]() {
			Q_ASSERT(m_isToTakeOutWafer.has_value());
			if (m_isToTakeOutWafer.value()) {
				emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"等待送wafer结束...");
			}
			else {
				emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"等待收wafer结束...");
			}
		});

		if (m_isToTakeOutWafer.value()) {
			emit sig_deviceConfirmCanTakeOutFromIt(MP_Public::MM_Maybe<bool>(true));
		}
		else {
			emit sig_deviceConfirmCanPutInToIt(MP_Public::MM_Maybe<bool>(true));
		}
	});
	QObject::connect(waitExeuteFinishedState, &QState::exited, this, [=]() {
		m_isWaitActionExecute = false;
	});


	//置解除锁定状态
	auto setUnLockedState = new QState(topState);
	QObject::connect(setUnLockedState, &QState::entered, this, [=]() {
		emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"解除锁定");
		QMetaObject::invokeMethod(getControl(), [=]() {
			getControl()->writeSingleVal(
				MI_Device::s_deviceBeLockedName,
				MS_DeviceBeLockedState::Not_Be_Locked,
				QOpcUa::Types::UInt16,
				[=](const MP_Public::ME_Error& _error) {
				emit this->sig_errorInfo(_error); },
				[=]() {
					postSignalEvent(curMachine, this, &MD_Dispenser::sig_unlockDeviceSuccess);
				});
		});
	});


	auto onAfterExecuteFinshedState = new QState(topState);
	QObject::connect(onAfterExecuteFinshedState, &QState::entered, this, [=]() {
		QMetaObject::invokeMethod(getControl(), [=]() {
			emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"置执行完成状态...");
			getControl()->writeSingleVal(
				MI_Device::s_deviceReceivceSendWaferCommandKeyName,
				MI_SendCommand::NOT_EXECUTE,
				QOpcUa::Types::UInt16,
				[=](const MP_Public::ME_Error& _error) {
				emit this->sig_errorInfo(_error); },
				[=]() {
					QMetaObject::invokeMethod(getControl(), [=]() {
						getControl()->writeSingleVal(
							MI_Device::s_deviceReceivceSendWaferCommandExecuteStateKeyName,
							MS_ExecuteState::FINIHED,
							QOpcUa::Types::UInt16,
							[=](const MP_Public::ME_Error& _error) {
							emit this->sig_errorInfo(_error); },
							[=]() {
								QMetaObject::invokeMethod(this, [=]() {
									if (m_isToTakeOutWafer.has_value()) {
										if (m_isToTakeOutWafer.value()) {
											emit sig_finishExecuteAferTakeOutWafer();
											emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"执行送wafer完成！");
										}
										else {
											emit sig_finishExecuteAferPutInWafer();
											emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"执行收wafer完成!");
										}
									}

									postSignalEvent(curMachine, this, &MD_Dispenser::sig_completeExecuteSuccess);
								});
							});
					});
				});
		});
	});



	auto failState = new QState();
	QObject::connect(failState, &QState::entered, this, [=]() {
		emit sig_logInfo(ML_LogLabel::ERROR_LABEL, QString(u8"点胶工位自动操作失败！"));
	});
	auto successState = new QState();

	auto finalState = new QFinalState();
	QObject::connect(finalState, &QState::entered, this, [=]() {
		m_isToTakeOutWafer = {};
		m_runMachine->stop();
	});

	auto errorState = new QState(topState);
	QObject::connect(errorState, &QState::entered, this, [=]() {
		emit sig_logInfo(ML_LogLabel::ERROR_LABEL, QString(u8"点胶工位进入故障状态！"));
		setCurrentDeviceIsErrorStatus(true);
		MA_Auxiliary::promptMessageBox(QMessageBox::Warning, u8"警告", QString(u8"%1点胶工位进入故障状态").arg(this->getName()));
	});

	//重连设备
	auto tryConnectDeviceState = new QState(topState);
	QObject::connect(tryConnectDeviceState, &QState::entered, this, [=]() {
		auto control{ getControl() };
		Q_ASSERT(control);
		setCurrentDeviceIsErrorStatus(false);
		if (control->getConnectState() == MS_ConnectState::CONNECTED) {
			//emit sig_noNeedTryConnect();
			emit sig_tryConnectedSuccess();
			return;
		}
	
		m_pollingConnectSuccessTimer->start(1000);
		tryConnectDevice();
		emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"重新连接设备...");
	});
	QObject::connect(tryConnectDeviceState, &QState::exited, this, [=]() {
		m_pollingConnectSuccessTimer->stop();
	});


	//初始化复位
	auto resetDeviceState = new QState(topState);
	QObject::connect(resetDeviceState, &QState::entered, this, [=]() {
		auto handle = MA_Auxiliary::onSingnalCallOnce(this, &MD_Dispenser::sig_deviceResetFinished, [=](const auto& _result) {
			if (_result.hasError()) {
				emit sig_logInfo(ML_LogLabel::ERROR_LABEL, u8"流程中复位失败！" + _result.getError()->getSelfMessage());
				emit sig_tryResetInProcessFail();
				return;
			}
			removeAllErrors();
			emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"设备从错误中恢复...");
			postSignalEvent(curMachine,this,&MD_Dispenser::sig_tryResetInProcessSuccess);
		});
		setStateOnExitAction(curMachine, resetDeviceState, [=]()mutable {
			handle.release();
		});
		resetDevice();
	});

	QObject::connect(resetDeviceState, &QState::exited, this, [=]() {
		executeStateOnExitAction(curMachine, resetDeviceState);
	});

	auto checkIfExistErrorState = new QState(topState);
	QObject::connect(checkIfExistErrorState, &QState::entered, this, [=]()
	{
		if(this->isExistError())
		{
			auto errors{this->getErrors()};
			QStringList errorInfo;
			for(const auto& var : errors)
			{
				errorInfo << var.getSelfMessage();
			}
			auto errorString{QString(u8"工位检查到存在异常！" + errorInfo.join(u8"	"))};
			emit sig_errorInfo({errorString});
			emit sig_existError();
			emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"工位检查到存在异常！" + errorInfo.join(u8"	"));
		}
		else
		{
			postSignalEvent(curMachine, this, &MD_Dispenser::sig_notExistError);
		}
	});

	

	topState->addTransition(this, &MD_Dispenser::sig_errorInfo, errorState);
	topState->addTransition(this, &MD_Dispenser::sig_deviceDisconnected, errorState);
	topState->addTransition(this,&MD_Dispenser::sig_requireStop,failState);

	errorState->addTransition(this, &MD_Dispenser::sig_tryRecoverFromErrorWhenRunningProcess, tryConnectDeviceState);
	tryConnectDeviceState->addTransition(this, &MD_Dispenser::sig_tryConnectedSuccess, resetDeviceState);
	resetDeviceState->addTransition(this, &MD_Dispenser::sig_tryResetInProcessFail, errorState);
	resetDeviceState->addTransition(this, &MD_Dispenser::sig_tryResetInProcessSuccess, checkIfExistErrorState);
	checkIfExistErrorState->addTransition(this, &MD_Dispenser::sig_existError, errorState);
	checkIfExistErrorState->addTransition(this, &MD_Dispenser::sig_notExistError, m_executePutInOrTakeOutWaferTopState);

	m_executePutInOrTakeOutWaferTopState->setInitialState(waitToExecuteState);

	waitToExecuteState->addTransition(this, &MD_Dispenser::sig_goStartPlanning, planState);

	planState->addTransition(this, &MD_Dispenser::sig_allowPlaned, waitExecuteCommandState);
	planState->addTransition(this, &MD_Dispenser::sig_notAllowPlaned, failState);

	waitExecuteCommandState->addTransition(this, &MD_Dispenser::sig_requiredExecuteReceiveAndSendWafer,setLockedState );
	setLockedState->addTransition(this, &MD_Dispenser::sig_lockDeviceSuccess, startExecuteReceiveAndSendWaferState);
	
	startExecuteReceiveAndSendWaferState->addTransition(this, &MD_Dispenser::sig_goWaitExecuteFinished, waitExeuteFinishedState);
	waitExeuteFinishedState->addTransition(this, &MD_Dispenser::sig_receiveAndSendWaferSuccessFinish, setUnLockedState);
	setUnLockedState->addTransition(this, &MD_Dispenser::sig_unlockDeviceSuccess, onAfterExecuteFinshedState);
	onAfterExecuteFinshedState->addTransition(this, &MD_Dispenser::sig_completeExecuteSuccess, m_executePutInOrTakeOutWaferTopState);


	failState->addTransition(finalState);
	
	successState->addTransition(finalState);

	curMachine->addState(topState);
	curMachine->setInitialState(topState);
	topState->setInitialState(checkIfExistErrorState);

	curMachine->addState(failState);
	curMachine->addState(successState);
	curMachine->addState(finalState);
}
//手动控制流程
void MD_Dispenser::initRunMachineInManual() {
	auto curMachine = m_runInManualMachine;
	
	auto topState = new QState();

	//等待
	auto waitToExecuteState = new QState(topState);
	QObject::connect(waitToExecuteState, &QState::entered, this, [=]() {
		m_isToTakeOutWafer = {};
		QTimer::singleShot(0, this, [=]() {
			m_isInWaitState = true;;
		});

		emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"等待执行...");
	});

	QObject::connect(waitToExecuteState, &QState::exited, this, [=]() {
		m_isInWaitState = false;
	});

	//规划
	auto planState = new QState(topState);
	QObject::connect(planState, &QState::entered, this, [=]() {
		emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"开始规划...");
		auto object = new QObject();
		setStateOnExitAction(curMachine, planState, [=]() {
			getControl()->disconnect(object);
			object->deleteLater();

			m_pollingPlanResultTimer->stop();
		});
		QObject::connect(getControl(), &MC_OpcDeviceControl::sig_startExecutePlanReceiveSendWaferResult, object, [=](const auto& _result) {
			if (_result.hasError()) {
				emit sig_errorInfo({ u8"Start execute plan fail!" });
				return;
			}
			m_pollingPlanResultTimer->start(300);
		});
		QMetaObject::invokeMethod(getControl(), [=]() {
			getControl()->planReceiveSendWafer();
		});
	});

	QObject::connect(planState, &QState::exited, this, [=]() {
		executeStateOnExitAction(curMachine, planState);
	});

	//等待执行指令
	auto waitExecuteCommandState = new QState(topState);
	QObject::connect(waitExecuteCommandState, &QState::entered, this, [=]() {
		emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"等待执行指令...");
		setStateOnExitAction(curMachine, waitExecuteCommandState, [=]() {
			m_pollingExecuteCommandTimer->stop();
		});
		m_pollingExecuteCommandTimer->start(100);
	});

	QObject::connect(waitExecuteCommandState, &QState::exited, this, [=]() {
		executeStateOnExitAction(curMachine, waitExecuteCommandState);
	});

	//置锁定状态
	auto setLockedState = new QState(topState);
	QObject::connect(setLockedState, &QState::entered, this, [=]() {
		emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"锁定设备");
		QMetaObject::invokeMethod(getControl(), [=]() {
			getControl()->writeSingleVal(
				MI_Device::s_deviceBeLockedName,
				MS_DeviceBeLockedState::Be_Loked,
				QOpcUa::Types::UInt16,
				[=](const MP_Public::ME_Error& _error) {
				emit this->sig_errorInfo(_error); },
				[=]() {
					postSignalEvent(curMachine, this, &MD_Dispenser::sig_lockDeviceSuccess);
				});
		});
	});

	//开始执行收送wafer
	auto startExecuteReceiveAndSendWaferState = new QState(topState);
	QObject::connect(startExecuteReceiveAndSendWaferState, &QState::entered, this, [=]() {
		emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"置执行中状态...");
		QMetaObject::invokeMethod(getControl(), [=]() {
			getControl()->writeSingleVal(
				MI_Device::s_deviceReceivceSendWaferCommandExecuteStateKeyName,
				MS_ExecuteState::EXECUTING,
				QOpcUa::Types::UInt16,
				[=](const MP_Public::ME_Error& _error) {
				emit this->sig_errorInfo(_error); },
				[=]() {
					postSignalEvent(curMachine, this, &MD_Dispenser::sig_goWaitExecuteFinished);
				});
		});
	});

	//等待结束
	auto waitExeuteFinishedState = new QState(topState);
	QObject::connect(waitExeuteFinishedState, &QState::entered, this, [=]() {
		m_isWaitActionExecute = true;
		QTimer::singleShot(0, this, [=]() {
			Q_ASSERT(m_isToTakeOutWafer.has_value());
			if (m_isToTakeOutWafer.value()) {
				emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"等待送wafer结束...");
			}
			else {
				emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"等待收wafer结束...");
			}
		});

		if (m_isToTakeOutWafer.value()) {
			emit sig_deviceConfirmCanTakeOutFromIt(MP_Public::MM_Maybe<bool>(true));
		}
		else {
			emit sig_deviceConfirmCanPutInToIt(MP_Public::MM_Maybe<bool>(true));
		}
	});
	QObject::connect(waitExeuteFinishedState, &QState::exited, this, [=]() {
		m_isWaitActionExecute = false;
	});


	//置解除锁定状态
	auto setUnLockedState = new QState(topState);
	QObject::connect(setUnLockedState, &QState::entered, this, [=]() {
		emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"解除锁定");
		QMetaObject::invokeMethod(getControl(), [=]() {
			getControl()->writeSingleVal(
				MI_Device::s_deviceBeLockedName,
				MS_DeviceBeLockedState::Not_Be_Locked,
				QOpcUa::Types::UInt16,
				[=](const MP_Public::ME_Error& _error) {
				emit this->sig_errorInfo(_error); },
				[=]() {
					postSignalEvent(curMachine, this, &MD_Dispenser::sig_unlockDeviceSuccess);
				});
		});
	});


	auto onAfterExecuteFinshedState = new QState(topState);
	QObject::connect(onAfterExecuteFinshedState, &QState::entered, this, [=]() {
		QMetaObject::invokeMethod(getControl(), [=]() {
			emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"置执行完成状态...");
			getControl()->writeSingleVal(
				MI_Device::s_deviceReceivceSendWaferCommandKeyName,
				MI_SendCommand::NOT_EXECUTE,
				QOpcUa::Types::UInt16,
				[=](const MP_Public::ME_Error& _error) {
				emit this->sig_errorInfo(_error); },
				[=]() {
					QMetaObject::invokeMethod(getControl(), [=]() {
						getControl()->writeSingleVal(
							MI_Device::s_deviceReceivceSendWaferCommandExecuteStateKeyName,
							MS_ExecuteState::FINIHED,
							QOpcUa::Types::UInt16,
							[=](const MP_Public::ME_Error& _error) {
							emit this->sig_errorInfo(_error); },
							[=]() {
								QMetaObject::invokeMethod(this, [=]() {
									if (m_isToTakeOutWafer.has_value()) {
										if (m_isToTakeOutWafer.value()) {
											emit sig_finishExecuteAferTakeOutWafer();
											emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"执行送wafer完成！");
										}
										else {
											emit sig_finishExecuteAferPutInWafer();
											emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"执行收wafer完成!");
										}
									}

									postSignalEvent(curMachine, this, &MD_Dispenser::sig_completeExecuteSuccess);
								});
							});
					});
				});
		});
	});



	auto failState = new QState();
	QObject::connect(failState, &QState::entered, this, [=]() {
		emit sig_logInfo(ML_LogLabel::ERROR_LABEL, QString(u8"点胶工位半自动操作失败结束！"));
	});
	auto successState = new QState();

	auto finalState = new QFinalState();
	QObject::connect(finalState, &QState::entered, this, [=]() {
		m_isToTakeOutWafer = {};
		setIsExecutingRunProcess(false);
		m_runInManualMachine->stop();
	});

	auto errorState = new QState(topState);

	//重连设备
	auto tryConnectDeviceState = new QState(topState);
	QObject::connect(tryConnectDeviceState, &QState::entered, this, [=]() {
		auto control{ getControl() };
		Q_ASSERT(control);
		if (control->getConnectState() == MS_ConnectState::CONNECTED) {
			emit sig_noNeedTryConnect();
			return;
		}

		m_pollingConnectSuccessTimer->start(1000);
		tryConnectDevice();
	});
	QObject::connect(tryConnectDeviceState, &QState::exited, this, [=]() {
		m_pollingConnectSuccessTimer->stop();
	});

	//初始化复位
	auto resetDeviceState = new QState(topState);
	QObject::connect(resetDeviceState, &QState::entered, this, [=]() {
		auto handle = MA_Auxiliary::onSingnalCallOnce(this, &MD_Dispenser::sig_deviceResetFinished, [=](const auto& _result) {
			if (_result.hasError()) {
				emit sig_logInfo(ML_LogLabel::ERROR_LABEL, u8"流程中复位失败！" + _result.getError()->getSelfMessage());
				emit sig_tryResetInProcessFail();
				return;
			}
			postSignalEvent(curMachine, this, &MD_Dispenser::sig_tryResetInProcessSuccess);
		});
		setStateOnExitAction(curMachine, resetDeviceState, [=]()mutable {
			handle.release();
		});
		resetDevice();
	});

	QObject::connect(resetDeviceState, &QState::exited, this, [=]() {
		executeStateOnExitAction(curMachine, resetDeviceState);
	});




	topState->addTransition(this, &MD_Dispenser::sig_errorInfo, errorState);
	topState->addTransition(this, &MD_Dispenser::sig_deviceDisconnected, errorState);
	topState->addTransition(this, &MD_Dispenser::sig_requireStopManualStateMachine, failState);

	errorState->addTransition(this, &MD_Dispenser::sig_tryRecoverFromErrorWhenRunningProcess, tryConnectDeviceState);

	waitToExecuteState->addTransition(this, &MD_Dispenser::sig_goStartPlanning, planState);

	planState->addTransition(this, &MD_Dispenser::sig_allowPlaned, waitExecuteCommandState);
	planState->addTransition(this, &MD_Dispenser::sig_notAllowPlaned, failState);

	waitExecuteCommandState->addTransition(this, &MD_Dispenser::sig_requiredExecuteReceiveAndSendWafer, setLockedState);
	setLockedState->addTransition(this, &MD_Dispenser::sig_lockDeviceSuccess, startExecuteReceiveAndSendWaferState);

	startExecuteReceiveAndSendWaferState->addTransition(this, &MD_Dispenser::sig_goWaitExecuteFinished, waitExeuteFinishedState);
	waitExeuteFinishedState->addTransition(this, &MD_Dispenser::sig_receiveAndSendWaferSuccessFinish, setUnLockedState);
	setUnLockedState->addTransition(this, &MD_Dispenser::sig_unlockDeviceSuccess, onAfterExecuteFinshedState);
	onAfterExecuteFinshedState->addTransition(this, &MD_Dispenser::sig_completeExecuteSuccess, successState);


	failState->addTransition(finalState);
	successState->addTransition(finalState);

	curMachine->addState(topState);
	curMachine->setInitialState(topState);
	topState->setInitialState(waitToExecuteState);

	curMachine->addState(failState);
	curMachine->addState(successState);
	curMachine->addState(finalState);
	
	////等待点胶机信号
	//auto m_waitSignalToDispenserState = new QState(topState);
	//QObject::connect(m_waitSignalToDispenserState, &QState::entered, this, [=]() {
	//	emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"MD_Disponser:等待点胶机信号状态");
	//});
	//QObject::connect(m_waitSignalToDispenserState, &QState::exited, this, [=]() {

	//});

	////判断点胶机发来的信号为去wafer或放wafer
	//auto m_playReceiveOrSendWaferState = new QState(topState);;
	//QObject::connect(m_playReceiveOrSendWaferState, &QState::entered, this, [=]() {

	//});
	//QObject::connect(m_playReceiveOrSendWaferState, &QState::exited, this, [=]() {

	//});

	////准备将wafer放入设备状态
	//auto m_preparePutInWaferState = new QState(topState);
	//QObject::connect(m_preparePutInWaferState, &QState::entered, this, [=]() {
	//	emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"MD_Disponser:准备将wafer放入设备r状态");
	//});
	//QObject::connect(m_preparePutInWaferState, &QState::exited, this, [=]() {

	//});

	////执行将wafer放入设备状态
	//auto m_executePutInWaferState = new QState(topState);
	//QObject::connect(m_executePutInWaferState, &QState::entered, this, [=]() {
	//	emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"MD_Disponser:执行将wafer放入设备状态");
	//});
	//QObject::connect(m_executePutInWaferState, &QState::exited, this, [=]() {

	//});

	////将wafer放入设备完成状态
	//auto m_putInWaferToDeviceFinishedState = new QState(topState);
	//QObject::connect(m_putInWaferToDeviceFinishedState, &QState::entered, this, [=]() {
	//	emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"MD_Disponser:将wafer放入设备完成状态");
	//});
	//QObject::connect(m_putInWaferToDeviceFinishedState, &QState::exited, this, [=]() {

	//});

	////准备将wafer取出设备状态
	//auto m_prepareTakeOutWaferState = new QState(topState);
	//QObject::connect(m_prepareTakeOutWaferState, &QState::entered, this, [=]() {
	//	emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"MD_Disponser:准备将wafer取出设备状态");
	//});
	//QObject::connect(m_prepareTakeOutWaferState, &QState::exited, this, [=]() {

	//});

	////执行将wafer取出设备状态
	//auto m_executeTakeOutWaferState = new QState(topState);
	//QObject::connect(m_executeTakeOutWaferState, &QState::entered, this, [=]() {
	//	emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"MD_Disponser:执行将wafer取出设备状态");
	//});
	//QObject::connect(m_executeTakeOutWaferState, &QState::exited, this, [=]() {

	//});
	//
	////将wafer取出设备完成状态
	//auto m_takeOutWaferToDeviceFinishedState = new QState(topState);
	//QObject::connect(m_takeOutWaferToDeviceFinishedState, &QState::entered, this, [=]() {
	//	emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"MD_Disponser:将wafer取出设备完成状态");
	//});
	//QObject::connect(m_takeOutWaferToDeviceFinishedState, &QState::exited, this, [=]() {

	//});

	//auto m_successState = new QState();
	//QObject::connect(m_successState, &QState::entered, this, [=]() {
	//	emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"MD_Disponser:点胶工位半自动流程成功完成！");
	//});
	//auto m_failState = new QState();
	//QObject::connect(m_failState, &QState::entered, this, [=]() {
	//	emit sig_logInfo(ML_LogLabel::NORMAL_LABEL, u8"MD_Disponser:点胶工位半自动流程失败结束！");
	//});
	//auto m_finishedState = new QFinalState();
	//QObject::connect(m_finishedState, &QState::entered, this, [=]() {
	//	curMachine->stop();
	//});
	//curMachine->addState(m_successState);;
	//curMachine->addState(m_failState);
	//curMachine->addState(m_finishedState);
	//curMachine->addState(topState);
	//curMachine->setInitialState(topState);
	//topState->setInitialState(m_waitSignalToDispenserState);


}
