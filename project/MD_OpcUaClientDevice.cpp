#include "MD_OpcUaClientDevice.h"
#include "MI_Device.h"
#include "MA_Auxiliary.h"
#include <QDebug>
#include <QMutexLocker>

using MP_Public::MM_MaybeOk;
using MP_Public::ME_Error;
template<typename T>
using MM_Maybe = MP_Public::MM_Maybe<T>;


QOpcUaProvider* MD_OpcUaClientDevice::s_opcUaProvider = new QOpcUaProvider();
QMutex MD_OpcUaClientDevice::s_opcUaProviderMutex;

MM_Maybe<QOpcUaClient*> MD_OpcUaClientDevice::getAvailableClient()
{
	QMutexLocker locker(&MD_OpcUaClientDevice::s_opcUaProviderMutex);
	auto backends = QOpcUaProvider::availableBackends();
	if (backends.empty())
	{
		return MM_Maybe<QOpcUaClient*>(ME_Error("No available backends!"));
	}

	auto client = s_opcUaProvider->createClient(backends.first());
	if (!client)
	{
		return MM_Maybe<QOpcUaClient*>(ME_Error("Create client fail!"));
	}
	return MM_Maybe<QOpcUaClient*>(client);
}



MD_OpcUaClientDevice::MD_OpcUaClientDevice(QObject *_parent)
	: QObject(_parent)
{
}

MD_OpcUaClientDevice::~MD_OpcUaClientDevice()
{
	if (m_opcuaClient && m_opcuaClient->state() == QOpcUaClient::Connected) {
		disconnectServer();
	}
	delete m_opcuaClient;
}

QUrl MD_OpcUaClientDevice::getServerUrl()
{
	return m_serverUrl;
}

MP_Public::MM_MaybeOk  MD_OpcUaClientDevice::createClient()
{
	if (m_opcuaClient)
	{
		return MM_MaybeOk();
	}
	auto clientResult = getAvailableClient();
	if (clientResult.getError()) {
		return MM_MaybeOk(*clientResult.getError());
	}
	m_opcuaClient = clientResult();

	connect(m_opcuaClient, &QOpcUaClient::connected, this, [=]()
	{
		emit this->sig_connected(MM_MaybeOk());
	});

	connect(m_opcuaClient, &QOpcUaClient::disconnected, this, &MD_OpcUaClientDevice::sig_disconnected);
	connect(m_opcuaClient, &QOpcUaClient::errorChanged, this, &MD_OpcUaClientDevice::sig_errorChanged);
	connect(m_opcuaClient, &QOpcUaClient::stateChanged, this, &MD_OpcUaClientDevice::sig_stateChanged);
	connect(m_opcuaClient, &QOpcUaClient::readNodeAttributesFinished, this, &MD_OpcUaClientDevice::sig_readNodeAttributesFinished);
	connect(m_opcuaClient, &QOpcUaClient::writeNodeAttributesFinished, this, &MD_OpcUaClientDevice::sig_writeNodeAttributesFinished);
	return MM_MaybeOk();
}

MP_Public::MM_MaybeOk MD_OpcUaClientDevice::tryConnectServer(const QHostAddress& _hostAddress, quint16 _port)
{
	QUrl url{ QLatin1String("opc.tcp://localhost:4840") };
	url.setHost(_hostAddress.toString());
	url.setPort(_port);
	m_serverUrl = url;

	if (!m_opcuaClient)
	{
		return MM_MaybeOk(ME_Error(u8"Client is null"));
	}

	auto onFailFunctor = [=](const QString& _error)
	{
		QMetaObject::invokeMethod(this, [=]()
		{
			emit sig_connected(ME_Error(_error));
		}, Qt::QueuedConnection);
	};

	auto object = new QObject();
	auto connection = connect(m_opcuaClient, &QOpcUaClient::findServersFinished, object, [=](const QVector<QOpcUaApplicationDescription> &servers, QOpcUa::UaStatusCode statusCode)
	{
		object->deleteLater();

		if (!isSuccessStatus(statusCode))
		{
			onFailFunctor(u8"Find server fail! " + statusToString(statusCode));
			return;
		}
		if (servers.empty())
		{
			onFailFunctor(u8"Cannot find server!");
			return;
		}

		auto urls = servers.front().discoveryUrls();
		if (urls.empty())
		{
			onFailFunctor(u8"Cannot find server url!");
			return;
		}
		auto endPointRequestObject = new QObject();
		QObject::connect(m_opcuaClient, &QOpcUaClient::endpointsRequestFinished, endPointRequestObject, [=](const QVector<QOpcUaEndpointDescription> &endpoints, QOpcUa::UaStatusCode statusCode)
		{
			endPointRequestObject->disconnect();
			ME_DestructExecuter executer([=]() {
				endPointRequestObject->deleteLater();
			});
			if (!isSuccessStatus(statusCode))
			{
				onFailFunctor(u8"Find end points fail!");
				return;
			}
			const char *modes[] = {
				"Invalid",
				"None",
				"Sign",
				"SignAndEncrypt"
			};
			std::vector<QOpcUaEndpointDescription> endPointsResult;
			for (const auto &endpoint : endpoints) {
				if (endpoint.securityMode() > sizeof(modes)) {
					qWarning() << "Invalid security mode";
					continue;
				}

				auto endpointName = QString("%1 (%2)")
					.arg(endpoint.securityPolicy(), modes[endpoint.securityMode()]);
				endPointsResult.emplace_back(endpoint);
			}

			if (endPointsResult.empty())
			{
				onFailFunctor(u8"Cannot find vaild end point!");
				return;
			}
			m_opcuaClient->connectToEndpoint(endPointsResult.front());

		});
		if (!m_opcuaClient->requestEndpoints(urls.first())) {
			qDebug()<< QString(u8"requestEndPoints:%1").arg(m_opcuaClient->error());
		}

	});
	m_opcuaClient->findServers(url, QStringList(), QStringList());
	return MM_MaybeOk();
}

QOpcUaClient::ClientState MD_OpcUaClientDevice::getConnectState()
{
	if (!m_opcuaClient)
	{
		return QOpcUaClient::ClientState::Disconnected;
	}
	return m_opcuaClient->state();
}

void MD_OpcUaClientDevice::disconnectServer()
{
	if (!m_opcuaClient)
	{
		return;
	}
	m_opcuaClient->disconnectFromEndpoint();

}

MM_MaybeOk MD_OpcUaClientDevice::updateArrayNamespace()
{
	if (!m_opcuaClient)
	{
		return MM_MaybeOk(ME_Error(u8"Client is null!"));
	}

	auto object = new QObject();
	QObject::connect(m_opcuaClient, &QOpcUaClient::namespaceArrayUpdated, object, [=](QStringList _namespace)
	{
		object->deleteLater();
		auto findIter = std::find_if(_namespace.begin(), _namespace.end(), [=](const QString& _val)
		{
			if (_val == MI_Device::s_nameSpaceName)
			{
				return true;
			}
			return false;
		});

		if (findIter == _namespace.end())
		{
			emit sig_updateArrayNamespaceFinished(MM_MaybeOk(ME_Error(u8"Cannot find namespace!")));
		}
		else
		{
			m_nameSpaceId = static_cast<quint16>(findIter - _namespace.begin());
			m_nodesMap.clear();
			emit sig_updateArrayNamespaceFinished(MM_MaybeOk());
		}

	});
	auto dispatchResult = m_opcuaClient->updateNamespaceArray();
	if (!dispatchResult)
	{
		object->deleteLater();
		auto dispatchError = MM_MaybeOk(ME_Error(u8"Dispatch update namespace array fail!"));
		emit sig_updateArrayNamespaceFinished(dispatchError);

		return dispatchError;
	}

	return MM_MaybeOk();
}

QOpcUaNode* MD_OpcUaClientDevice::getNode(const QString& _valeName)
{
	return getNode(m_nameSpaceId, _valeName);
}

QOpcUaNode* MD_OpcUaClientDevice::getNode(quint16 _namespaceId, const QString& _name)
{

	if (!m_opcuaClient || m_opcuaClient->state() != QOpcUaClient::Connected)
	{
		qDebug() << u8"Client is not connected!";
		return nullptr;
	}

	auto nodeId = QOpcUa::nodeIdFromString(_namespaceId, _name);

	auto iter = m_nodesMap.find(nodeId);
	if (iter == m_nodesMap.end())
	{
		auto node = m_opcuaClient->node(nodeId);
		if (!node)
		{
			return node;
		}
		m_nodesMap[nodeId] = node;
		return node;
	}

	return iter->second;
}

MP_Public::MM_MaybeOk MD_OpcUaClientDevice::readNodeAttributes(const QVector<QOpcUaReadItem>& _nodesToRead)
{
	if (m_opcuaClient->readNodeAttributes(_nodesToRead))
	{
		return MP_Public::MM_MaybeOk();
	}
	else
	{
		return MP_Public::MM_MaybeOk(MP_Public::ME_Error(u8"Read node attributes dispatch fail!"));
	}
}

MP_Public::MM_MaybeOk MD_OpcUaClientDevice::writeNodeAttributes(const QVector<QOpcUaWriteItem> &_nodesToWrite)
{
	if (m_opcuaClient->writeNodeAttributes(_nodesToWrite))
	{
		return MP_Public::MM_MaybeOk();
	}
	else
	{
		return MP_Public::MM_MaybeOk(MP_Public::ME_Error(u8"Write nodes attributes dispatch fail!"));
	}
}


