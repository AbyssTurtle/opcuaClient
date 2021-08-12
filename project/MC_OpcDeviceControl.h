#pragma once
#include "MI_Device.h"
#include "MM_Maybe.h"
#include "MS_StateMachineAuxiliary.h"
#include "ML_LogBase.h"
#include <QHostAddress>
#include <QPointer>
#include <QObject>
#include <map>
#include <memory>
#include <functional>
#include <atomic>
#include <QString>
#include <QVariant>
#include <QDateTime>
#include <QMutex>
#include <qopcuatype.h>
#include <utility>

class MC_OpcUaClient;
class QStateMachine;
class QState;
class QTimer;



class MC_OpcDeviceControl : public ML_LogBase, public MS_StateMachineAuxiliary
{
	Q_OBJECT

public:
	MC_OpcDeviceControl(const QString& _name, QObject *_parent = nullptr);
	virtual ~MC_OpcDeviceControl();


	MT_PC100CommunicationDeviceType getCommunicationDeviceType() const { return m_communicationDeviceType; }
	void setCommuicationDeviceType(MT_PC100CommunicationDeviceType _val) { m_communicationDeviceType = _val; }


	quint16 getInitCommandExecuteState() const { return m_initCommandExecuteState.load(); }
	void setInitCommandExecuteState(quint16 _val);

	quint16 getPlanReceiveAndSendWaferStateRespond()const { return m_planReceiveAndSendWaferStateRespond.load(); }
	void setPlanReceiveAndSendWaferStateRespond(quint16 _val);


	quint16 getReceiveAndSendWaferCommandExecuteState() const { return m_receiveAndSendWaferCommandExecuteState.load(); }
	void setReceiveAndSendWaferCommandExecuteState(quint16 _val);

	quint16 getDeviceState() const { return m_deviceState.load(); }
	void setDeviceState(quint16 _val);

	quint16 getDeviceWorkAreaWorkState() const { return m_deviceWorkAreaWorkState.load(); }
	void setDeviceWorkAreaWorkState(quint16 _val);


	quint16 getDeviceWorkAreaIfHasWaferState() const { return m_deviceWorkAreaIfHasWaferState.load(); }
	void setDeviceWorkAreaIfHasWaferState(quint16 val);

	quint16 getDeviceReadyToReceiveAndSendWaferState() const { return m_deviceReadyToReceiveAndSendWaferState; }
	void setDeviceReadyToReceiveAndSendWaferState(quint16 _val);

	quint16 getReceiveAndSendWaferCommand() const { return m_receiveAndSendWaferCommand.load(); }
	void setReceiveAndSendWaferCommand(quint16 _val);

	void writeSingleVal(
		const QString& _fieldName,
		const QVariant& _val,
		QOpcUa::Types _valtype,
		std::function<void(MP_Public::ME_Error const & _val)> _onError,
		std::function<void()> _onSuccess);


	void writeMultiVals(
		std::vector<std::pair<QString, std::pair<QOpcUa::Types, QVariant>>> const& _vals,
		std::function<void(MP_Public::ME_Error const & _val)> _onError,
		std::function<void()> _onSuccess);





signals:
	//连接服务器结果信号
	void sig_connectResult(const MP_Public::MM_MaybeOk& _result);

	void sig_connectStateChanged(MS_ConnectState _val);

	//设备类型检测结果信号
	void sig_deviceTypeCheckResult(const MP_Public::MM_Maybe<bool>& _result);

	//初始化指令发送结果信号
	void sig_executeInitCommandResult(const MP_Public::MM_MaybeOk& _result);
	//初始化指令执行状态改变信号
	void sig_initCommnandExecuteStateChanged(quint16 _state);

	//收送wafer就绪信号
	void sig_readReadyToReceiveSendWaferStateResult(const MP_Public::MM_Maybe<quint16>& _result);
	//规划收送板执行结果信号
	void sig_startExecutePlanReceiveSendWaferResult(const MP_Public::MM_MaybeOk& _result);
	//规划收板状态应答信号
	void sig_planReceiveAndSendWaferRespondChanged(quint16 _state);
	//收板指令发送结果信号
	void sig_startExecuteReceiveSendWaferCommandResult(const MP_Public::MM_MaybeOk& _result);
	//收板指令执行状态改变信号
	void sig_receiveAndSendCommandExecuteStateChanged(quint16 _state);
	//取消规划收板信号
	void sig_cancelPlanReceiveSendWaferResult(const MP_Public::MM_MaybeOk& _val);


	//工位状态改变信号
	void sig_deviceStateChanged(quint16 _state);

	//工位作业区作业状态改变信号
	void sig_deviceWorkAreaWorkStateChanged(quint16 _state);

	//工位作业区有无Wafer状态改变信号
	void sig_deviceWorkAreaIfHasWaferStateChanged(quint16 _state);

	//收送wafer就绪状态改变信号
	void sig_readyToReceiveSendWaferStateChanged(qint16 _val);

	//收送wafer指令改变信号
	void sig_readyToReceiveSendWaferCommandValueChanged(qint16 _val);


	public slots:

	//连接服务器
	void tryConnect(const QHostAddress& _serverIpAddress, quint16 _port);

	//断开连接
	void disconnectServer();

	MS_ConnectState getConnectState();

	//校验设备类型
	void checkDeviceType();
	//初始化指令
	void executeInitCommand();
	//读取收送wafer就绪
	void readReadyToReceiveAndSendWaferState();
	//收送wafer规划
	void planReceiveSendWafer();
	//收送wafer指令
	void executeReceiveSendWaferCommand();

	//收送wafer取消规划
	void cancelPlanReceviceSendWafer();


	protected slots :

	void clearConnectionMakeWhenConnection();
	virtual	void makeNodesValChangedConnections();


	void makeNodeValueChangedConnection(const QString& _fieldName, std::function<void(quint16)> _onValChanedFun);





protected:
	//收送板相关连接
	virtual void makeReceiveSendWaferValConnections();

	void cancelPlan(
		const QString& _planStateFieldName,
		const QString& _planCommandFieldName,
		std::function<void(MP_Public::ME_Error const & _val)> _onError,
		std::function<void()> _onSuccess);



	void executePlanNode(const QString& _planRespondFieldName, const QString& _beInPlanFieldName, std::function<void(MP_Public::ME_Error const & _val)> _onError, std::function<void()> const & _onSuccess);
	void readVal(QString const& _fieldName, std::function<void(MP_Public::ME_Error const & _error)> const & _onFail, std::function<void(QVariant const & _val)> const & _onSuccess);
	void startExecuteCommand(const QString& _logHead,
		const QString& _executeStateField,
		const QString& _executeCommandField,
		std::function<void(const MP_Public::MM_MaybeOk&)> _resultFun,
		std::function<void()> _updateStateFun);


	void startExecuteCommand(
		const QString& _logHead,
		const std::vector<std::pair<QString, std::function<bool(quint16)>>> & _readChecks,
		const QString& _executeCommandField,
		std::function<void(const MP_Public::MM_MaybeOk&)> const & _resultFun,
		std::function<void()> _updateStateFun,
		const std::vector<std::pair<QString, std::pair<QOpcUa::Types, QVariant>>>& _valsWriteBeforeExecuteCommand = {},
		quint16 _executeCommandVal = MI_SendCommand::NEED_EXECUTE);

	void readMultiVal(std::vector<QString> const& _keyNames, std::function<void(MP_Public::ME_Error const & _error)> const & _onFail, std::function<void(std::map<QString, QVariant> const & _val)> const & _onSuccess);

	//名字
	QString m_name;

	//设备通讯类型
	MT_PC100CommunicationDeviceType m_communicationDeviceType = MT_PC100CommunicationDeviceType::SW_TYPE;

	//OpcUa
	std::shared_ptr<MC_OpcUaClient> m_client;


	//连接分控时建立的信号连接
	std::vector<QMetaObject::Connection> m_clientConnectedConnections;

	//初始化指令执行状态
	std::atomic<quint16> m_initCommandExecuteState = MS_ExecuteState::NOT_EXECUTE;
	//收送晶圆规划状态应答
	std::atomic<quint16> m_planReceiveAndSendWaferStateRespond = MI_PlanRespond::INIT_VALUE;

	//送送晶圆执行指令
	std::atomic<quint16>  m_receiveAndSendWaferCommand = MI_SendCommand::NOT_EXECUTE;

	//收送晶圆指令执行状态
	std::atomic<quint16> m_receiveAndSendWaferCommandExecuteState = MS_ExecuteState::NOT_EXECUTE;
	//工位状态
	std::atomic<quint16> m_deviceState = MS_DeviceState::STATE_RESETTING;
	//工位作业区作业状态
	std::atomic<quint16> m_deviceWorkAreaWorkState = MS_DeviceWorkAreaWorkState::STATE_NONE_WORK;
	//工位作业区有无wafer状态
	std::atomic<quint16> m_deviceWorkAreaIfHasWaferState = MS_DeviceWorkAreaIfHasWaferState::STATE_NONE;

	//工位收送Wafer就绪状态
	std::atomic<quint16> m_deviceReadyToReceiveAndSendWaferState = MS_IsReadyReceiveAndSendWaferState::NOT_READY;


	//分控作业区开始作业时间
	QDateTime m_deviceWorkAreaStartWorkDateTime;
	QMutex m_deviceWorkAreaStartWorkDateTimeMutex;




};
