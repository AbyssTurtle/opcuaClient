#include "MC_OpcDeviceControl.h"
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


MC_OpcDeviceControl::MC_OpcDeviceControl(const QString& _name, QObject *_parent)
	: ML_LogBase(_name, _parent), MS_StateMachineAuxiliary(),
	m_client(new MC_OpcUaClient())
{

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


	QObject::connect(m_client.get(), &MC_OpcUaClient::sig_log, this, [=](const auto& _name, ML_LogLabel _label, const QString & _log) {
		emit sig_log(_name, _label, _log);
	});

}

void MC_OpcDeviceControl::makeNodesValChangedConnections()
{
	//初始化指令执行状态
	makeNodeValueChangedConnection(MI_Device::s_initCommandExecuteStateName, [=](quint16 _val)
	{
		setInitCommandExecuteState(_val);
	});



	makeReceiveSendWaferValConnections();

	//工位状态
	makeNodeValueChangedConnection(MI_Device::s_deviceStateName, [=](quint16 _val)
	{
		setDeviceState(_val);
	});

	//设备作业区是否有工装
	makeNodeValueChangedConnection(MI_Device::s_deviceWorkAreaIfHasWaferName, [=](quint16 _val)
	{
		setDeviceWorkAreaIfHasWaferState(_val);
	});

	//作业区状态
	makeNodeValueChangedConnection(MI_Device::s_deviceWorkAreaWorkStateName, [=](quint16 _val)
	{
		setDeviceWorkAreaWorkState(_val);
	});

}


void MC_OpcDeviceControl::makeReceiveSendWaferValConnections()
{
	//收送wafer就绪
	makeNodeValueChangedConnection(MI_Device::s_deviceIsReadyReceivceSendWaferKeyName, [=](qint16 _val)
	{
		setDeviceReadyToReceiveAndSendWaferState(_val);
	});

	//收送wafer规划应答
	makeNodeValueChangedConnection(MI_Device::s_deviceReceiveSendWaferBeInPlanningRespondKeyName, [=](qint16 _val)
	{
		setPlanReceiveAndSendWaferStateRespond(_val);
	});

	//收板指令执行状态
	makeNodeValueChangedConnection(MI_Device::s_deviceReceivceSendWaferCommandExecuteStateKeyName, [=](quint16 _val)
	{
		setReceiveAndSendWaferCommandExecuteState(_val);
	});

	//收板指令
	makeNodeValueChangedConnection(MI_Device::s_deviceReceivceSendWaferCommandKeyName, [=](quint16 _val)
	{
		setReceiveAndSendWaferCommand (_val);
	});
}

MC_OpcDeviceControl::~MC_OpcDeviceControl()
{
}


void MC_OpcDeviceControl::setDeviceReadyToReceiveAndSendWaferState(quint16 _val)
{
	if (getDeviceReadyToReceiveAndSendWaferState() == _val)
	{
		return;
	}

	m_deviceReadyToReceiveAndSendWaferState = _val;

	emit sig_readyToReceiveSendWaferStateChanged(_val);
}


void MC_OpcDeviceControl::setDeviceWorkAreaIfHasWaferState(quint16 _val)
{
	if (getDeviceWorkAreaIfHasWaferState() == _val)
	{
		return;
	}
	m_deviceWorkAreaIfHasWaferState = _val;
	emit this->sig_deviceWorkAreaIfHasWaferStateChanged(_val);
}

void MC_OpcDeviceControl::setDeviceWorkAreaWorkState(quint16 _val)
{

	if (getDeviceWorkAreaWorkState() == _val)
	{
		return;
	}
	m_deviceWorkAreaWorkState = _val;
	emit this->sig_deviceWorkAreaWorkStateChanged(_val);

}

void MC_OpcDeviceControl::setReceiveAndSendWaferCommandExecuteState(quint16 _val)
{
	if (getReceiveAndSendWaferCommandExecuteState() == _val)
	{
		return;
	}
	m_receiveAndSendWaferCommandExecuteState = _val;
	emit this->sig_receiveAndSendCommandExecuteStateChanged(_val);
}

void MC_OpcDeviceControl::setDeviceState(quint16 _val)
{
	if (getDeviceState() == _val)
	{
		return;
	}
	m_deviceState = _val;
	emit this->sig_deviceStateChanged(_val);
}

void MC_OpcDeviceControl::makeNodeValueChangedConnection(const QString& _fieldName, std::function<void(quint16)> _onValChanedFun)
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

void MC_OpcDeviceControl::setInitCommandExecuteState(quint16 _val)
{

	if (getInitCommandExecuteState() == _val)
	{
		return;
	}
	m_initCommandExecuteState = _val;
	emit this->sig_initCommnandExecuteStateChanged(_val);

}

void MC_OpcDeviceControl::setPlanReceiveAndSendWaferStateRespond(quint16 _val)
{
	if (getPlanReceiveAndSendWaferStateRespond() == _val)
	{
		return;
	}

	m_planReceiveAndSendWaferStateRespond = _val;
	emit this->sig_planReceiveAndSendWaferRespondChanged(_val);
}

void MC_OpcDeviceControl::tryConnect(const QHostAddress& _serverIpAddress, quint16 _port)
{
	if (_serverIpAddress.isNull())
	{
		return;
	}
	m_client->createAndConnectServer(_serverIpAddress, _port);
}

void MC_OpcDeviceControl::disconnectServer()
{
	clearConnectionMakeWhenConnection();
	m_client->disConnectServer();

}

MS_ConnectState MC_OpcDeviceControl::getConnectState()
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

void MC_OpcDeviceControl::checkDeviceType()
{
	auto watch = m_client->readNodeVariable(MI_Device::s_deviceTypeString);
	QObject::connect(watch, &MN_Public::MC_FutureWatchBase::finished, this, [=]()
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

void MC_OpcDeviceControl::startExecuteCommand(
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
	QObject::connect(readValsBeforeExecuteWatch, &MN_Public::MC_FutureWatchBase::finished, this, [=]()
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
			QObject::connect(executeWatch, &MN_Public::MC_FutureWatchBase::finished, this, [=]()
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
			QObject::connect(setValsStateWath, &MN_Public::MC_FutureWatchBase::finished, this, [=]()
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

void MC_OpcDeviceControl::startExecuteCommand(
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

void MC_OpcDeviceControl::readMultiVal(std::vector<QString> const& _keyNames, std::function<void(MP_Public::ME_Error const & _error)> const & _onFail, std::function<void(std::map<QString, QVariant> const & _val)> const & _onSuccess)
{
	auto watch = m_client->readMultiNodeVariables(_keyNames);
	QObject::connect(watch, &MN_Public::MC_FutureWatchBase::finished, this, [=]()
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


void MC_OpcDeviceControl::executeInitCommand()
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

void MC_OpcDeviceControl::readVal(QString const& _fieldName,
	std::function<void(ME_Error const & _error)> const & _onFail,
	std::function<void(QVariant const & _val)> const & _onSuccess)
{
	auto watch = m_client->readNodeVariable(_fieldName);
	QObject::connect(watch, &MN_Public::MC_FutureWatchBase::finished, this, [=]()
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

void MC_OpcDeviceControl::readReadyToReceiveAndSendWaferState()
{
	readVal(MI_Device::s_deviceIsReadyReceivceSendWaferKeyName,
		[=](ME_Error const & _error)
	{
		emit this->sig_readReadyToReceiveSendWaferStateResult(_error);
	},
		[=](QVariant const & _val)
	{
		auto stateVal = _val.value<quint16>();

		if (stateVal != MS_IsReadyReceiveAndSendWaferState::NOT_READY) {
			emit this->sig_readReadyToReceiveSendWaferStateResult(MM_Maybe<quint16>(stateVal));
		}

	});
}


void MC_OpcDeviceControl::executePlanNode(const QString& _planRespondFieldName,
	const QString& _beInPlanFieldName,
	std::function<void(MP_Public::ME_Error const & _val)> _onError,
	std::function<void()> const & _onSuccess
)
{
	//置规划状态应答初值
	auto setPlanRespondWatch = m_client->writeNodeVariable(_planRespondFieldName, MI_PlanRespond::INIT_VALUE, QOpcUa::Types::UInt16);
	QObject::connect(setPlanRespondWatch, &MN_Public::MC_FutureWatchBase::finished, this, [=]()
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
		QObject::connect(setPlanStateWatch, &MN_Public::MC_FutureWatchBase::finished, this, [=]()
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

void MC_OpcDeviceControl::planReceiveSendWafer()
{

	executePlanNode(MI_Device::s_deviceReceiveSendWaferBeInPlanningRespondKeyName,
		MI_Device::s_deviceReceiveSendWaferBeInPlanningKeyName,
		[=](ME_Error const _val)
	{
		emit sig_startExecutePlanReceiveSendWaferResult(_val);
	}, [=]()
	{
		emit sig_startExecutePlanReceiveSendWaferResult(MM_MaybeOk());
	});
}


void MC_OpcDeviceControl::executeReceiveSendWaferCommand()
{
	startExecuteCommand(u8"Send receivceSend wafer command: ",
		MI_Device::s_deviceReceivceSendWaferCommandExecuteStateKeyName,
		MI_Device::s_deviceReceivceSendWaferCommandKeyName,
		[=](MM_MaybeOk const & _val)
	{
		emit sig_startExecuteReceiveSendWaferCommandResult(_val);
	},
		[=]() {
		setReceiveAndSendWaferCommandExecuteState(MS_ExecuteState::NOT_EXECUTE);
	}
	);
}

void MC_OpcDeviceControl::cancelPlanReceviceSendWafer()
{
	cancelPlan(MI_Device::s_deviceReceiveSendWaferBeInPlanningRespondKeyName,
		MI_Device::s_deviceReceiveSendWaferBeInPlanningKeyName,
		[this](const ME_Error& _val)
	{
		emit this->sig_cancelPlanReceiveSendWaferResult(_val);
	},
		[this]()
	{
		emit this->sig_cancelPlanReceiveSendWaferResult(MM_MaybeOk());
	});

}



void MC_OpcDeviceControl::cancelPlan(const QString& _planStateFieldName,
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

void MC_OpcDeviceControl::setReceiveAndSendWaferCommand(quint16 _val)
{
	if (getReceiveAndSendWaferCommand() == _val)
	{
		return;
	}

	m_receiveAndSendWaferCommand = _val;

	emit sig_readyToReceiveSendWaferCommandValueChanged(_val);
}

void MC_OpcDeviceControl::writeSingleVal(
	const QString& _fieldName,
	const QVariant& _val,
	QOpcUa::Types _valtype,
	std::function<void(MP_Public::ME_Error const & _val)> _onError,
	std::function<void()> _onSuccess)
{
	auto watch = m_client->writeNodeVariable(_fieldName, _val, _valtype);
	QObject::connect(watch, &MN_Public::MC_FutureWatchBase::finished, this, [=]()
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

void MC_OpcDeviceControl::writeMultiVals(std::vector<std::pair<QString, std::pair<QOpcUa::Types, QVariant>>> const& _vals, std::function<void(MP_Public::ME_Error const & _val)> _onError, std::function<void()> _onSuccess)
{
	auto watch = m_client->writeMultiNodeVariables(_vals);
	QObject::connect(watch, &MN_Public::MC_FutureWatchBase::finished, this, [=]()
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

void MC_OpcDeviceControl::clearConnectionMakeWhenConnection()
{
	for (auto& var : m_clientConnectedConnections)
	{
		QObject::disconnect(var);
	}
	m_clientConnectedConnections.clear();
	m_clientConnectedConnections.shrink_to_fit();
}



