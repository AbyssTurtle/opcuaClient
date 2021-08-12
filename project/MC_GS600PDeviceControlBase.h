#pragma once
#include "MI_Device.h"
#include "MM_Maybe.h"
#include "MI_ToolingIdentifier.h"
#include "MR_WorkToolingData.h"
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
class MC_FutureWatchBase;
class QTimer;



class MC_GS600PDeviceControlBase : public ML_LogBase,public MS_StateMachineAuxiliary
{
	Q_OBJECT

public:
	MC_GS600PDeviceControlBase(QObject *_parent = nullptr);
	virtual ~MC_GS600PDeviceControlBase();

	enum ME_TransitionKeyWordType {
		BeReadyState,//过渡就绪状态
		BeInPlanningState,//过渡规划状态
		BeInPlanningRespond,//过渡规划状态应答
		Command,//过渡
		ExecuteState,//过渡执行状态
		FinishResult,//过渡结果

	};

	MT_GS600PCommunicationDeviceType getCommunicationDeviceType() const { return m_communicationDeviceType; }
	void setCommuicationDeviceType(MT_GS600PCommunicationDeviceType _val) { m_communicationDeviceType = _val; }

	quint16 getInitCommandExecuteState() const { return m_initCommandExecuteState.load(); }
	quint16 getPlanReceiveToolingStateRespond() const { return m_planReceiveToolingStateRespond.load(); }

	quint16 getReceiveToolingCommandExecuteState() const { return m_receiveToolingCommandExecuteState.load(); }
	quint16 getPlanSendToolingStateRespond() const { return m_planSendToolingStateRespond.load(); }
	quint16 getSendToolingCommandExecuteState() const { return m_sendToolingCommandExecuteState; }
	quint16 getDeviceState() const { return m_deviceState; }
	quint16 getDeviceWorkAreaWorkState() const { return m_deviceWorkAreaWorkState; }
	quint16 getDeviceWorkAreaIfHasToolingState() const { return m_deviceWorkAreaIfHasToolingState; }
	quint16 getDeviceIfShowMainControlUiState() const { return m_deviceIfShowMainControlUiState; }
	quint16 getDeviceIsRequireDataState() const { return m_deviceIsRequireDataState; }
	quint16 getDeviceIsRequireUploadDataState() const { return m_deviceIsRequireUploadDataState; }
	
	bool getIsMonitorShowMainUiFlag() const { return isMonitorShowMainUiFlag; }
	void setIsMonitorShowMainUiFlag(bool _val) { isMonitorShowMainUiFlag = _val; }
	QDateTime getDeviceWorkAreaStartWorkDateTime();
	void setDeviceWorkAreaStartWorkDateTime(const QDateTime& _val);
	quint16 getDeviceReadyToReceiveInState() const { return m_deviceReadyToReceiveInState.load(); }
	quint16 getDeviceReadyToSendOutState() const { return m_deviceReadyToSendOutState.load(); }


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
	
	QDateTime getDeviceReadyToReceiveInDateTime();
	void setDeviceReadyToReceiveInDateTime(const QDateTime& val);
	
	QDateTime getDeviceReadyToSendOutDateTime();
	void setDeviceReadyToSendOutDateTime(const QDateTime& val);
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

	//收板就绪信号
	void sig_readReadyToReceiveToolingStateResult(const MP_Public::MM_Maybe<bool>& _result);
	//规划收板执行结果信号
	void sig_startExecutePlanReceiveToolingResult(const MP_Public::MM_MaybeOk& _result);
	//规划收板状态应答信号
	void sig_planReceiveToolingRespondStateChanged(quint16 _state);
	//收板指令发送结果信号
	void sig_startExecuteReceiveToolingCommandResult(const MP_Public::MM_MaybeOk& _result);
	//收板指令执行状态改变信号
	void sig_receiveToolingCommandExecuteStateChanged(quint16 _state);
	//取消规划收板信号
	void sig_cancelPlanReceiveToolingResult(const MP_Public::MM_MaybeOk& _val);

	//送板就绪信号
	void sig_readReadyToSendToolingStateResult(const MP_Public::MM_Maybe<bool>& _result);
	//规划送板结果信号
	void sig_startExecutePlanSendToolingResult(const MP_Public::MM_MaybeOk& _result);
	//规划送板状态应答信号
	void sig_planSendToolingRespondStateChanged(quint16 _state);
	//送板指令发送结果信号
	void sig_startExecuteSendToolingCommandResult(const MP_Public::MM_MaybeOk& _result);
	//送板指令执行状态改变信号
	void sig_sendToolingCommandExecuteStateChanged(quint16 _state);
	//取消规划送板信号
	void sig_cancelPlanSendToolingResult(const MP_Public::MM_MaybeOk& _val);

	//工位状态改变信号
	void sig_deviceStateChanged(quint16 _state);
	
	//工位作业区作业状态改变信号
	void sig_deviceWorkAreaWorkStateChanged(quint16 _state);

	//工位作业区开始作业时间改变信号
	void sig_deviceWorkAreaStartWorkDateTimeChanged(const QDateTime& _val);

	//工位作业区有无板状态改变信号
	void sig_deviceWorkAreaIfHasToolingStateChanged(quint16 _state);

	//读取工位是否配置主控信号
	void sig_readDeviceIfConfigMainControlResult(const MP_Public::MM_Maybe<bool>& _val);
	//是否显示主控界面改变信号
	void sig_deviceIfShowMainUiChanged(quint16 _val);

	//请求数据信号
	void sig_hasReceiveDeviceRequireDataCommand();
	void sig_deviceRequireData(const MI_ToolingIdentifier& _val);

	//请求上传数据信号
	void sig_hasReceiveDeviceRequireUploadDataCommand();
	void sig_deviceUploadData(const MR_WorkToolingData& _data);

	//送板就绪状态改变信号
	void sig_readyToSendOutToolingStateChanged(qint16 _val);

	//收板就绪状态改变信号
	void sig_readyToReceiveInToolingStateChanged(quint16 _val);


public slots:

	//连接服务器
	void tryConnect(const QHostAddress& _serverIpAddress,quint16 _port);

	//断开连接
	void disconnectServer();
	
	MS_ConnectState getConnectState();

	//校验设备类型
	void checkDeviceType();
	//初始化指令
	void executeInitCommand();
	//读取收板就绪
	void readReadyToReceiveToolingState();
	//收板规划
	void planReceiveTooling();
	//收板指令
	void executeReceiveToolingCommand();

	//收板取消规划
	void cancelPlanReceviceTooling();

	//读取送板就绪
	void readReadyToSendToolingState();
	//收板规划
	void planSendTooling();
	//收板指令
	void executeSendToolingCommand();
	//送板取消规划
	void cancelPlanSendTooling();
	
	//读取是否配置主控
	void readDeviceIfConfigMainControl();

	//发送工装数据
	void onSendToolingData(const MR_WorkToolingData& _data, bool _isDataValid, bool _isDoAll = false );

	//上传完数据
	void onHasUploadData();

	//开始等待执行数据请求指令
	void startWaitExecuteRequireData();
	//停止执行数据请求指令
	void stopExecuteRequireData();

	//开始等待执行上传数据指令
	void startWaitUploadData();
	//停止执行上传数据指令
	void stopExecuteUploadData();


	protected slots :
	
	void clearConnectionMakeWhenConnection();
    virtual	void makeNodesValChangedConnections();


	void makeNodeValueChangedConnection(const QString& _fieldName, std::function<void(quint16)> _onValChanedFun);
	
	void setInitCommandExecuteState(quint16 _val);
	void setPlanReceiveToolingStateRespond(quint16 _val);
	void setReceiveToolingCommandExecuteState(quint16 _val);
	void setPlanSendToolingStateRespond(quint16 _val);
	void setSendToolingCommandExecuteState(quint16 _val); 
	void setDeviceState(quint16 _val);
	void setDeviceWorkAreaWorkState(quint16 _val);
	void setDeviceWorkAreaIfHasToolingState(quint16 _val);
	void setDeviceIfShowMainControlUiState(quint16 _val); 
	void setDeviceIsRequireDataState(quint16 _val);
	void setDeviceIsRequireUploadDataState(quint16 _val);
	
	void setDeviceReadyToSendOutState(quint16 _val); 
	void setDeviceReadyToReceiveInState(quint16 _val); 
	void initOnClientRequireDataMachine();
	
	void initOnClinetUploadDataMachine();

	void readDeviceIfUploadDataUntilSuccess();
	void readDeviceIfRequireDataUntilSuccess();



protected:
	//数据相关连接
	virtual void makeDataConnections();
	//送板相关连接
	virtual void makeSendToolingValConnections();
	//收板相关连接
	virtual void makeReceiveToolingValConnections();

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


	//设备通讯类型
	MT_GS600PCommunicationDeviceType m_communicationDeviceType = MT_GS600PCommunicationDeviceType::GS600P_TYPE;
	
	//OpcUa
	std::shared_ptr<MC_OpcUaClient> m_client;

	bool isMonitorShowMainUiFlag = false;

	//请求数据
	QStateMachine* m_onClientRequireDataMachine = nullptr;
	
	//上传数据
	QStateMachine* m_onClientUploadDataMachine = nullptr;

	//连接分控时建立的信号连接
	std::vector<QMetaObject::Connection> m_clientConnectedConnections;

	//初始化指令执行状态
	std::atomic<quint16> m_initCommandExecuteState = MS_ExecuteState::NOT_EXECUTE;
	//收板规划状态应答
	std::atomic<quint16> m_planReceiveToolingStateRespond = MI_PlanRespond::INIT_VALUE;
	//收板指令执行状态
	std::atomic<quint16> m_receiveToolingCommandExecuteState = MS_ExecuteState::NOT_EXECUTE;
	//送板规划状态应答
	std::atomic<quint16> m_planSendToolingStateRespond = MI_PlanRespond::INIT_VALUE;
	//送板指令执行状态
	std::atomic<quint16> m_sendToolingCommandExecuteState = MS_ExecuteState::NOT_EXECUTE;
	//工位状态
	std::atomic<quint16> m_deviceState = MS_DeviceState::STATE_RESETTING;
	//工位作业区作业状态
	std::atomic<quint16> m_deviceWorkAreaWorkState = MS_DeviceWorkAreaWorkState::STATE_NONE_WORK;
	//工位作业区有无板状态
	std::atomic<quint16> m_deviceWorkAreaIfHasToolingState = MS_DeviceWorkAreaIfHasToolingState::STATE_NONE;
	//是否显示主控
	std::atomic<quint16> m_deviceIfShowMainControlUiState = MS_DeviceIfShowMainControlUI::NOT_EXECUTE;

	//工位收板就绪状态
	std::atomic<quint16> m_deviceReadyToReceiveInState = MS_IsReadyReceiveAndSendToolingState::NOT_READY;
	
	//工位送板就绪状态
	std::atomic<quint16> m_deviceReadyToSendOutState = MS_IsReadyReceiveAndSendToolingState::NOT_READY;


	//分控请求数据状态
	std::atomic<quint16> m_deviceIsRequireDataState = MS_DeviceRequireDataState::NOT_REQUIRE;

	//分控请求上传数据状态
	std::atomic<quint16> m_deviceIsRequireUploadDataState = MS_DeviceReuireUploadDataState::NOT_REQUIRE;

	//分控作业区开始作业时间
	QDateTime m_deviceWorkAreaStartWorkDateTime;
	QMutex m_deviceWorkAreaStartWorkDateTimeMutex;

	QDateTime m_deviceReadyToReceiveInDateTime = QDateTime::currentDateTime();
	QMutex m_deviceReadyToReceiveInDateTimeMutex;
	QDateTime m_deviceReadyToSendOutDateTime = QDateTime::currentDateTime();
	QMutex m_deviceReadyToSendOutDateTimeMutex;

	QTimer* m_onCheckRequireDataTimer{};
	QTimer* m_onCheckRequireUploadTimer{};

	QString getTransitionKeyString(ME_TransitionKeyWordType _type);
	std::map<ME_TransitionKeyWordType, QString> m_transitionKeyWordMap;

};
