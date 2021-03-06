#pragma once

#include "MI_DeviceInterface.h"
#include "MI_Device.h"
#include "MM_Maybe.h"
#include "MP_DispenserParam.h"
#include "MS_StateMachineAuxiliary.h"
#include <QString>
#include <QObject>

class MC_OpcDeviceControl;
class MS_StateMachine;
class QTimer;
class QThread;

struct MS_DispenserHardwareSet {
	MP_ReportWarningInputIOInfo m_dispenserLightCurtainAlarmInputIO;
	

	std::vector<MP_ReportWarningInputIOInfo> getReportErrorInputIOInfos()const {
		return{ m_dispenserLightCurtainAlarmInputIO };
	}
};

class MD_Dispenser : public MI_DeviceInterface, public MS_StateMachineAuxiliary
{
	Q_OBJECT
public:
	MD_Dispenser(const QString& _name, QObject* _parent = nullptr);
	~MD_Dispenser();
	
	MN_PC100::MI_DeviceInfo::MT_DeviceType getCurrentDeviceType() override { return m_currentDeviceType; }
	MN_PC100::MI_DeviceInteractionStatus::MT_DeviceIsSuspended getCurrentDeviceIsSuspended() override { return m_currentDeviceIsSuspended; }
	void setCurrentDeviceIsSuspended(MN_PC100::MI_DeviceInteractionStatus::MT_DeviceIsSuspended _var) override { m_currentDeviceIsSuspended = _var; }

	MP_Public::MM_MaybeOk initDevice()override;

	bool tryConnectDevice() override;

	bool isDeviceConnected()override;

	bool disconnectDevice() override;

	void resetDevice()override;

	void inquireIfHasWaferToTakeOut() override;

	double getTransportPosition() const override;
	bool pauseDevice() override;
	bool resumeRunDevice() override;
	bool stopDevice() override;

	bool startExecuteProcess() override;
	bool startExecuteManualProcess() override;
	void stopManualStateMachine() override;
	
	bool isReadyTakenOutWafer() override;
	bool isReadyTakenOutWaferInManual() override;
	bool checkIfReadyTakeOut();
	bool checkIfReadyTakeOutInManual();
	bool isReadyPutInWafer(MW_Wafer* _wafer) override;
	bool isReadyPutInWaferInManual() override;
	MN_PC100::MI_DeviceInteractionStatus::MT_DeviceIsReady isReadyPutInOrTakeOutWafer() override;
	bool checkIfReadyToPutIn();
	bool checkIfReadyToPutInInManual();
	void onFinishPutInWafer() override;


	void onFinishTakeOutWafer() override;
	void requireDeviceConfirmCanPutInWaferToDeviceAtOnce(std::shared_ptr<MW_Wafer> _wafer) override;
	void requireDeviceConfirmCanPutInWaferToDeviceAtOnceInManual() override;
	void requireDeviceConfirmCanTakeOutWaferFromDeviceAtOnce() override;
	void requireDeviceConfirmCanTakeOutWaferFromDeviceAtOnceInManual() override;
	void unlockForTransportWafer() override;

	bool operateSafetyDoorSwitch(bool _var) override { return true; }
	bool acquireSafetyDoorSwitchStatus() override { return true; }

	//????????????????????????????????????
	void updateDevicesInformationInHomePage(MN_PC100::MI_DeviceInteractionStatus& _var)override;

	void setIsExistWaferInManual(bool _isExistWaferInManual) override { m_isExistWaferInManual = _isExistWaferInManual; }
	bool getIsExistWaferInManual() override { return m_isExistWaferInManual; }

	void setCurrentDeviceIsErrorStatus(bool _isError) { m_currentDeviceIsErrorStatus = _isError; }
	bool getCurrentDeviceIsErrorStatus() const override { return m_currentDeviceIsErrorStatus; }

	/**
	 * ??????IP
	 */
	QHostAddress getHostAddress() const;

	/**
	* ???????????????
	*/
	quint16 getPort()const;
	MP_DispenserParam getParam() const { return m_param; }
	MP_DispenserParam& getParam() { return m_param; }
	void setParam(const MP_DispenserParam& val) { m_param = val; }

	void writeLogFileForParam(const QString& _var) override;

	bool getLigthCurtainAlarmInputIO() const override { return m_ligthCurtainAlarmInputIO; }
	void setLigthCurtainAlarmInputIO( bool val) override { m_ligthCurtainAlarmInputIO = val; }

	MS_DispenserHardwareSet getHardwareSet() const { return m_hardwareSet; }
	void setHardwareSet(const MS_DispenserHardwareSet& val) { m_hardwareSet = val; }
signals:
	//????????????????????????
	void sig_connectStateChanged(MS_ConnectState _state);

	void sig_executeOperateStateChanged(const QString& _val);
	void sig_canStartReceiveAndSendWafer();//????????????????????????wafer??????
	void sig_receiveAndSendWaferSuccessFinish();//??????wafer??????

	//machine
	void sig_allowPlaned();//??????????????????
	void sig_notAllowPlaned();//?????????????????????
	void sig_requiredExecuteReceiveAndSendWafer();//??????????????????wafer
	void sig_goWaitExecuteFinished();//??????????????????
	void sig_completeExecuteSuccess();//??????????????????

	void sig_requireStop();
	void sig_goStartPlanning();

	void sig_lockDeviceSuccess();
	void sig_unlockDeviceSuccess();

	void sig_tryConnectedSuccess(); //????????????
	void sig_noNeedTryConnect();//???????????????

	void sig_tryResetInProcessSuccess();//??????????????????????????????
	void sig_tryResetInProcessFail();//??????????????????????????????
	void sig_existError();
	void sig_notExistError();
	void sig_updateDevicesInformationInHomePage(const MN_PC100::MI_DeviceInteractionStatus& _var);

	//?????????????????????
	void sig_preparePutInWaferInManual();
	void sig_prepareTakeOutWaferInManual();
	void sig_putInWaferToDeviceInManual();
	void sig_takeOutWaferToDeviceInManual();
	void sig_putInWaferFinishedInManual();
	void sig_takeOutWaferFinishedInManual();


private:
	const MN_PC100::MI_DeviceInfo::MT_DeviceType m_currentDeviceType = MN_PC100::MI_DeviceInfo::MT_DeviceType::DISPENSER_TYPE;
	MN_PC100::MI_DeviceInteractionStatus::MT_DeviceIsSuspended m_currentDeviceIsSuspended = MN_PC100::MI_DeviceInteractionStatus::MT_DeviceIsSuspended::AWAKEN;//???????????????????????????
	MC_OpcDeviceControl* getControl()const { return m_control.get(); };

	std::shared_ptr<MC_OpcDeviceControl> m_control;
	std::shared_ptr<QThread> m_controlThread;

	QString m_name;

	MP_DispenserParam m_param;//??????

	void log(ML_LogLabel _label, const QString& _logInfo);

	MS_StateMachine* m_runMachine{};
	MS_StateMachine* m_runInManualMachine{};

	boost::optional<bool> m_isToTakeOutWafer;//??????????????????
	std::atomic_bool m_isInWaitState = false; //?????????????????????

	bool m_isWaitActionExecute = false;//???????????????????????????

	QTimer* m_pollingPlanResultTimer{}; //??????????????????
	QTimer* m_pollingExecuteCommandTimer{};//??????????????????
	QTimer* m_pollingInitCommandFinishTimer{};//???????????????????????????
	QTimer* m_pollingConnectSuccessTimer{};//??????????????????

	void initRunMachine();
	void initRunMachineInManual();
	bool m_isExistWaferInManual{ false };//????????????????????????????????????????????????wafer

	MS_DispenserHardwareSet m_hardwareSet;
	bool m_ligthCurtainAlarmInputIO{ true };   //?????????????????????

	bool m_currentDeviceIsErrorStatus { false };   //?????????????????????????????????
};
