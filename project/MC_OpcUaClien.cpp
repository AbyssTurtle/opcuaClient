#include "MC_OpcUaClient.h"
#include "MD_OpcUaClientDevice.h"
#include "MC_FutureWatchResultProvider.h"
#include "MA_Auxiliary.h"
#include <QDebug>
#include <functional>
#include <QTimer>

using MP_Public::MM_MaybeOk;
using MP_Public::ME_Error;
using MP_Public::MM_Maybe;
MC_OpcUaClient::MC_OpcUaClient(QObject *parent)
	: ML_LogBase(parent),
	m_control(new MD_OpcUaClientDevice()),
	m_enableMonitorTimer(new QTimer(this))
{
	QObject::connect(m_control.get(), &MD_OpcUaClientDevice::sig_stateChanged, this, [=](QOpcUaClient::ClientState _state)
	{
		emit this->sig_connectStateChanged(convertState(_state));
	});

	m_enableMonitorTimer->setInterval(1000);
	QObject::connect(m_enableMonitorTimer, &QTimer::timeout, this, [=]() {
		//检查是否有没在监控中的
		for (const auto& var : m_monitorKeyWords) {

			auto node = getNode(var.first);
			if (node) {
				if (node->monitoringStatus(QOpcUa::NodeAttribute::Value).statusCode() == QOpcUa::UaStatusCode::Good) {
					continue;
				}
				else
				{
					auto object = new QObject();
					QObject::connect(node, &QOpcUaNode::enableMonitoringFinished, object, [=](QOpcUa::NodeAttribute attr, QOpcUa::UaStatusCode statusCode) {

						Q_UNUSED(attr);
				
						ME_DestructExecuter executer([=]() {
							object->disconnect();
							object->deleteLater();
						});

						if (statusCode == QOpcUa::UaStatusCode::Good) {

							auto readObject = new QObject();
							QObject::connect(node, &QOpcUaNode::attributeRead, readObject, [=]() {
								auto iter = std::find_if(m_monitorKeyWords.begin(), m_monitorKeyWords.end(), [=](const auto& _a) {
									return _a.first == var.first;
								});
								if (iter != m_monitorKeyWords.end()) {
									iter->second = true;
								}
							});

							node->readAttributes(QOpcUa::NodeAttribute::Value
								| QOpcUa::NodeAttribute::NodeClass
								| QOpcUa::NodeAttribute::Description
								| QOpcUa::NodeAttribute::DataType
								| QOpcUa::NodeAttribute::BrowseName
								| QOpcUa::NodeAttribute::DisplayName
								| QOpcUa::NodeAttribute::Value
							);


						}
					});

					QOpcUaMonitoringParameters monitorParam(100);
					monitorParam.setMonitoringMode(QOpcUaMonitoringParameters::MonitoringMode::Reporting);
					monitorParam.setDiscardOldest(true);
					node->enableMonitoring(QOpcUa::NodeAttribute::Value
						, monitorParam);
				}
			}
		}

		if (std::all_of(m_monitorKeyWords.begin(), m_monitorKeyWords.end(), [=](const auto& _a) {
			auto node = getNode(_a.first);
			if (node) {
				return node->monitoringStatus(QOpcUa::NodeAttribute::Value).statusCode() == QOpcUa::UaStatusCode::Good;
			}
			return false;
		})
			&&
			std::all_of(m_monitorKeyWords.begin(), m_monitorKeyWords.end(), [=](const auto& _a) {
			return _a.second;
		}))
		{
			log(ML_LogLabel::NORMAL_LABEL, u8"开启监控成功!");
			m_enableMonitorTimer->stop();

			logFile(ML_LogLabel::NORMAL_LABEL, u8"监控项:");
			auto ip = m_control->getServerUrl().toString();
			for (const auto& var : m_monitorKeyWords) {
				logFile(ML_LogLabel::NORMAL_LABEL, ip + u8" : " + var.first);
			}
		}

	});
}

MC_OpcUaClient::~MC_OpcUaClient()
{
}

void MC_OpcUaClient::createAndConnectServer(const QHostAddress& _hostAddress, quint16 _port)
{
	auto onFailFunctor = [=](const ME_Error& _error)
	{
		emit this->sig_connectResult(_error);
		log(ML_LogLabel::WARNING_LABEL, _error.getMessage());
	};

	QMetaObject::invokeMethod(m_control.get(), [=]()
	{
		auto createClientResult = m_control->createClient();
		if (createClientResult.hasError())
		{
			onFailFunctor(*createClientResult.getError());
			return;
		}

	
		auto object = new QObject();
		QObject::connect(m_control.get(), &MD_OpcUaClientDevice::sig_connected, object, [=](const MM_MaybeOk& _connectResult)
		{
			ME_DestructExecuter onDeleteObject([=]() {
				object->disconnect();
				object->deleteLater();
			});

			if (_connectResult.hasError())
			{
				onFailFunctor(*_connectResult.getError());
				return;
			}
			auto  retObject = new QObject();

			QObject::connect(m_control.get(), &MD_OpcUaClientDevice::sig_updateArrayNamespaceFinished, retObject, [=](const MM_MaybeOk& _updateNamespaceArrayResult)
			{
				retObject->deleteLater();
				if (_updateNamespaceArrayResult.hasError())
				{
					onFailFunctor(*_updateNamespaceArrayResult.getError());
					return;
				}

				emit this->sig_connectResult(MM_MaybeOk());

			});

			auto updateArrayNamesapceResult = m_control->updateArrayNamespace();
			if (updateArrayNamesapceResult.hasError())
			{
				onFailFunctor(*updateArrayNamesapceResult.getError());
				return;
			}

		});
		auto connectServerResult = m_control->tryConnectServer(_hostAddress, _port);
		if (connectServerResult.hasError())
		{
			onFailFunctor(*connectServerResult.getError());
			return;
		}
	});
}

void MC_OpcUaClient::disConnectServer()
{
	if (!m_control)
	{
		return;
	}
	m_control->disconnectServer();
}


void MC_OpcUaClient::clearMonitorWords()
{
	m_monitorKeyWords.clear();
	m_monitorKeyWords.shrink_to_fit();
}

MC_FutureWatch<QVariant>* MC_OpcUaClient::readNodeVariable(const QString& _keyName)
{
	//if (QThread::currentThread() == this->thread())
	//{
		return getReadNodeVariableWatch(_keyName);
	//}


	//std::promise<MC_FutureWatch<QVariant>*> promise;
	//QMetaObject::invokeMethod(this, [=, &promise]()
	//{
	//	auto ret = getReadNodeVariableWatch(_keyName);
	//	promise.set_value(ret);
	//});

	//return promise.get_future().get();
}

MC_FutureWatch<QVariant>* MC_OpcUaClient::getReadNodeVariableWatch(const QString& _keyName)
{

	auto object = new QObject();
	auto watch(new MC_FutureWatch<QVariant>());

	auto onFailFun = [=](const QString& _val)
	{
		MC_FutureWatchResultProvider provider;
		provider.setIsSuccess(*watch, false);
		provider.setErrorInfo(*watch, _val);
		provider.setFutureWatchFinished(*watch);
	};

	auto node = this->getNode(_keyName);
	if (!node)
	{
		onFailFun(u8"Read node: node is null!");
		return watch;
	}

	QObject::connect(node, &QOpcUaNode::attributeRead, object, [=](QOpcUa::NodeAttributes attributes)
	{
		Q_UNUSED(attributes);

		ME_DestructExecuter onDeleteObject([=]() {
			object->disconnect();
			object->deleteLater();
		});

		auto curNodeAttributeUsed = QOpcUa::NodeAttribute::Value;
		if (node->attributeError(curNodeAttributeUsed) != QOpcUa::UaStatusCode::Good) {
			onFailFun(u8"Failed to read attribute: " + statusToString(node->attributeError(curNodeAttributeUsed)));
			return;
		}

		auto val = node->attribute(curNodeAttributeUsed);
		if (val.canConvert<quint16>())
		{

			MC_FutureWatchResultProvider provider;
			provider.setIsSuccess(*watch, true);
			provider.setResult(*watch, val);
			provider.setFutureWatchFinished(*watch);
		}
		else
		{
			onFailFun(u8"Read value attribute: value type is not right!");
		}

	});

	auto readAttributesResult = node->readAttributes(QOpcUaNode::mandatoryBaseAttributes() | QOpcUa::NodeAttribute::Value);
	if (!readAttributesResult)
	{
		onFailFun(u8"Dispatch read attribute fail!");
	}
	return watch;
}

MC_FutureWatch<void>* MC_OpcUaClient::writeNodeVariable(const QString& _keyName, const QVariant& _val, QOpcUa::Types _type)
{

	//if (QThread::currentThread() == this->thread())
	//{
		return getWriteNodeVariableWatch(_keyName, _val, _type);
	//}


	//std::promise<MC_FutureWatch<void>*> promise;
	//QMetaObject::invokeMethod(this, [=, &promise]()
	//{
	//	auto ret = getWriteNodeVariableWatch(_keyName, _val, _type);
	//	promise.set_value(ret);
	//});

	//return promise.get_future().get();
}


MC_FutureWatch<std::map<QString, QVariant>>* MC_OpcUaClient::readMultiNodeVariables(const std::vector<QString>& _keyNames)
{
	//if (QThread::currentThread() == this->thread())
	//{
		return getReadMultiNodeVariablesWatch(_keyNames);
	//}


	//std::promise<MC_FutureWatch<std::map<QString, QVariant>>*> promise;
	//QMetaObject::invokeMethod(this, [=, &promise]()
	//{
	//	auto ret = getReadMultiNodeVariablesWatch(_keyNames);
	//	promise.set_value(ret);
	//});

	//return promise.get_future().get();
}


MC_FutureWatch<void>* MC_OpcUaClient::writeMultiNodeVariables(const std::vector<std::pair<QString, std::pair<QOpcUa::Types, QVariant>>>& _vals)
{
	//if (QThread::currentThread() == this->thread())
	//{
		return getWriteMultiNodeVariablesWatch(_vals);
	//}


	//std::promise<MC_FutureWatch<void>*> promise;
	//QMetaObject::invokeMethod(this, [=, &promise]()
	//{
	//	auto ret = getWriteMultiNodeVariablesWatch(_vals);
	//	promise.set_value(ret);
	//});

	//return promise.get_future().get();

}

QOpcUaNode* MC_OpcUaClient::getNode(const QString& _keyName)
{
	return m_control->getNode(_keyName);
}

QOpcUaNode* MC_OpcUaClient::getNode(quint16 _namespace, const QString& _keyName)
{
	return m_control->getNode(_namespace, _keyName);
}

MS_ConnectState MC_OpcUaClient::getConnectState()
{
	if (!m_control)
	{
		return MS_ConnectState::DISCONNECTED;
	}
	auto state = m_control->getConnectState();
	return convertState(state);
}

MS_ConnectState MC_OpcUaClient::convertState(QOpcUaClient::ClientState state)
{
	switch (state)
	{
	case QOpcUaClient::Disconnected:
		return MS_ConnectState::DISCONNECTED;
		break;
	case QOpcUaClient::Connecting:
		return MS_ConnectState::CONNECTING;
		break;
	case QOpcUaClient::Connected:
		return MS_ConnectState::CONNECTED;
		break;
	case QOpcUaClient::Closing:
		return MS_ConnectState::DISCONNECTING;
		break;
	default:
		break;
	}

	return  MS_ConnectState::UNKNOW;
}

MC_FutureWatch<void>* MC_OpcUaClient::getWriteNodeVariableWatch(const QString& _keyName, const QVariant& _val, QOpcUa::Types _type)
{

	auto object = new QObject();
	auto watch(new MC_FutureWatch<void>());

	auto onFailFun = [=](const QString& _val)
	{
		MC_FutureWatchResultProvider provider;
		provider.setIsSuccess(*watch, false);
		provider.setErrorInfo(*watch, _val);
		provider.setFutureWatchFinished(*watch);
	};

	auto node = this->getNode(_keyName);
	if (!node)
	{
		onFailFun(u8"Write node: node is null！");
		return watch;
	}

	QObject::connect(node, &QOpcUaNode::attributeWritten, object, [=](QOpcUa::NodeAttributes attributes)
	{

		Q_UNUSED(attributes);
		ME_DestructExecuter onDeleteObject([=]() {
			object->disconnect();
			object->deleteLater();
		});

		auto curNodeAttributeUsed = QOpcUa::NodeAttribute::Value;
		if (node->attributeError(curNodeAttributeUsed) != QOpcUa::UaStatusCode::Good) {
			onFailFun(u8"Failed to write attribute: " + statusToString(node->attributeError(curNodeAttributeUsed)));
		}
		else
		{
			MC_FutureWatchResultProvider provider;
			provider.setIsSuccess(*watch, true);
			provider.setFutureWatchFinished(*watch);;
		}
	});

	auto writeAttributesResult = node->writeAttribute(QOpcUa::NodeAttribute::Value, _val, _type);
	if (!writeAttributesResult)
	{
		onFailFun("Dispatch write attribute fail!");
	}
	return watch;
}

MC_FutureWatch<std::map<QString, QVariant>>* MC_OpcUaClient::getReadMultiNodeVariablesWatch(const std::vector<QString>& _keyNames)
{
	auto object  = new QObject();
	auto watch(new MC_FutureWatch<std::map<QString, QVariant>>());

	auto onFailFun = [=](const QString& _val)
	{
		MC_FutureWatchResultProvider provider;
		provider.setIsSuccess(*watch, false);
		provider.setErrorInfo(*watch, _val);
		provider.setFutureWatchFinished(*watch);
	};


	if (_keyNames.empty())
	{
		onFailFun(u8"Fail to read nodes attributes : key names is empty!");
		return watch;
	}

	QObject::connect(m_control.get(), &MD_OpcUaClientDevice::sig_readNodeAttributesFinished, object, [=](QVector<QOpcUaReadResult> _results, QOpcUa::UaStatusCode _serviceResult)
	{
		ME_DestructExecuter onDeleteObject([=]() {
			object->disconnect();
			object->deleteLater();
		});

		if (_serviceResult != QOpcUa::UaStatusCode::Good)
		{
			onFailFun(u8"Fail to read nodes attributes (read service):" + statusToString(_serviceResult));
			return;
		}

		if (_results.size() != _keyNames.size())
		{
			onFailFun(u8"Fail to read nodes attributes: result size is not right!");
			return;
		}

		std::map<QString, QVariant> ret;
		for (auto curIndex = 0; curIndex < _results.size(); ++curIndex)
		{
			auto curItemStatus{ _results.at(curIndex).statusCode() };
			if (curItemStatus != QOpcUa::UaStatusCode::Good)
			{
				onFailFun(u8"Fail to read nodes attributes: result item status is not good! " + _keyNames.at(curIndex) + " : " + statusToString(curItemStatus));
				return;
			}
			ret[_keyNames.at(curIndex)] = _results.at(curIndex).value();
		}

		MC_FutureWatchResultProvider provider;
		provider.setIsSuccess(*watch, true);
		provider.setResult(*watch, ret);
		provider.setFutureWatchFinished(*watch);
	});


	QVector <QOpcUaReadItem> readItems;
	for (const auto& var : _keyNames)
	{
		readItems.push_back(QOpcUaReadItem(QOpcUa::nodeIdFromString(m_control->getNamespaceId(), var), QOpcUa::NodeAttribute::Value));
	}

	auto readAttributesResult = m_control->readNodeAttributes(readItems);
	if (readAttributesResult.hasError())
	{
		onFailFun(readAttributesResult.getError()->getMessage());
	}
	return watch;

}

MC_FutureWatch<void>* MC_OpcUaClient::getWriteMultiNodeVariablesWatch(const std::vector<std::pair<QString, std::pair<QOpcUa::Types, QVariant>>>& _vals)
{
	auto object = new QObject();
	auto watch(new MC_FutureWatch<void>());

	auto onFailFun = [=](const QString& _val)
	{
		MC_FutureWatchResultProvider provider;
		provider.setIsSuccess(*watch, false);
		provider.setErrorInfo(*watch, _val);
		provider.setFutureWatchFinished(*watch);
	};

	if (_vals.empty())
	{
		onFailFun(u8"Write nodes attributes: items to write is empty! ");
		return watch;
	}


	QObject::connect(m_control.get(), &MD_OpcUaClientDevice::sig_writeNodeAttributesFinished, object, [=](QVector<QOpcUaWriteResult> _results, QOpcUa::UaStatusCode _serviceResult)
	{
		ME_DestructExecuter onDeleteObject([=]() {
			object->disconnect();
			object->deleteLater();
		});

	
		if (_serviceResult != QOpcUa::UaStatusCode::Good)
		{
			onFailFun(u8"Fail to write nodes attributes (write service):" + statusToString(_serviceResult));
			return;
		}

		if (_results.size() != _vals.size())
		{
			onFailFun(u8"Fail to read node attributes: result size is not right!");
			return;
		}

		for (auto curIndex = 0; curIndex < _results.size(); ++curIndex)
		{
			auto curItemStatus{ _results.at(curIndex).statusCode() };
			if (curItemStatus != QOpcUa::UaStatusCode::Good)
			{
				onFailFun(u8"Fail to write nodes attributes: result item status is not good! "
					+ _vals.at(curIndex).first + "( index :" + QString::number(curIndex) + " )"
					+ " : " + statusToString(curItemStatus));
				return;
			}
		}
		MC_FutureWatchResultProvider provider;
		provider.setIsSuccess(*watch, true);
		provider.setFutureWatchFinished(*watch);
	
	});

	QVector<QOpcUaWriteItem> itemsToWrite;
	for (const auto& var : _vals)
	{
		itemsToWrite.push_back(QOpcUaWriteItem(
			QOpcUa::nodeIdFromString(m_control->getNamespaceId(),
				var.first), QOpcUa::NodeAttribute::Value, var.second.second, var.second.first));
	}
	auto writeAttributesResult = m_control->writeNodeAttributes(itemsToWrite);
	if (writeAttributesResult.hasError())
	{
		onFailFun(writeAttributesResult.getError()->getMessage());
	}
	return watch;
}

void MC_OpcUaClient::addMonitorKeyWord(const QString& _val)
{
	if (std::find_if(m_monitorKeyWords.begin(), m_monitorKeyWords.end(), [=](const auto& _a) {
		return _a.first == _val;
	}) == m_monitorKeyWords.end())
	{
		m_monitorKeyWords.emplace_back(std::make_pair(_val, false));
		m_enableMonitorTimer->start();
	}
}
