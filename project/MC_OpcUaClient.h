#pragma once

#include "MM_Maybe.h"
#include "MC_FutureWatch.h"
#include "MI_Device.h"
#include "ML_LogBase.h"
#include <QHostAddress>
#include <QObject>
#include <QtOpcUa>
#include <memory>
#include <map>
#include <utility>

class MD_OpcUaClientDevice;
class QTimer;

class MC_OpcUaClient : public ML_LogBase
{
	Q_OBJECT

public:
	MC_OpcUaClient(QObject *parent = nullptr);
	~MC_OpcUaClient();

signals:
	void sig_connectResult(const MP_Public::MM_MaybeOk& _isConnectOk);
	void sig_connectStateChanged(MS_ConnectState _state);

public slots:
	void createAndConnectServer(const QHostAddress& _hostAddress, quint16 _port);
	void disConnectServer();

	void clearMonitorWords();
	
	MC_FutureWatch<QVariant>* readNodeVariable(const QString& _keyName);
	MC_FutureWatch<void>*  writeNodeVariable(const QString& _keyName, const QVariant& _val, QOpcUa::Types _type);

	MC_FutureWatch<std::map<QString, QVariant>>* readMultiNodeVariables(const std::vector<QString>& _keyNames);
	MC_FutureWatch<void>* writeMultiNodeVariables(const std::vector<std::pair<QString, std::pair<QOpcUa::Types, QVariant>>>& _vals);

	QOpcUaNode* getNode(const QString& _keyName);

	QOpcUaNode* getNode(quint16 _namespace,const QString& _keyName);

	MS_ConnectState getConnectState();

	void addMonitorKeyWord(const QString& _val);

	private slots:

	MS_ConnectState convertState(QOpcUaClient::ClientState state);

	MC_FutureWatch<QVariant>* getReadNodeVariableWatch(const QString& _keyName);
	MC_FutureWatch<void>* getWriteNodeVariableWatch(const QString& _keyName, const QVariant& _val, QOpcUa::Types _type);

	MC_FutureWatch<std::map<QString, QVariant>>* getReadMultiNodeVariablesWatch(const std::vector<QString>& _keyNames);
	MC_FutureWatch<void>* getWriteMultiNodeVariablesWatch(const std::vector<std::pair<QString, std::pair<QOpcUa::Types, QVariant>>>& _vals);


private:
	std::shared_ptr<MD_OpcUaClientDevice> m_control = nullptr;

	std::vector<std::pair<QString, bool>> m_monitorKeyWords;
	QTimer* m_enableMonitorTimer{};

};
