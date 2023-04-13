#include "network.h"

//------------------------------------------------------------------------------

using namespace MLB;
using namespace Network;

//------------------------------------------------------------------------------

std::map<void*, QSemaphore> NetworkBase::m_threadKillSem;

//------------------------------------------------------------------------------
/*
 *
 */
MLB::NetworkBase::NetworkBase(QObject* parent) : QObject(parent)
{
    m_threadKillSem[thread()].release();
}
/*
 *
 */
bool MLB::NetworkBase::moveToThread(QThread* thread)
{
    if (parent() == nullptr)
    {
        auto oldThread = QObject::thread();
        QObject::moveToThread(thread);
        m_threadKillSem[oldThread].acquire();
        m_threadKillSem[thread].release();
        return true;
    }
    else
    {
        qDebug() << "[" << udp_socket_name << "]"
                 << "To move socket to another thread, "
                 << "it must not have a parent!";
        return false;
    }
}
/*
 *
 */
void MLB::NetworkBase::__killNetworkThread__()
{
    m_threadKillSem[thread()].acquire();

    if (m_threadKillSem[thread()].available())
        return;

    if (qApp->thread() != thread())
        thread()->exit();
}

//------------------------------------------------------------------------------
/*
 *
 */
MLB::AsynchSocketInterface::AsynchSocketInterface(QObject* parent)
    : QObject(parent)
{
    NetworkUdp* loader = new NetworkUdp();
    loader->moveToThread(&m_thread);

    connect(this, &AsynchSocketInterface::__killSockets__,
            loader, &NetworkUdp::__killSockets__);                              // Поток отвечает за очистку памяти, выделенную под управляющий сокетом объект
    connect(loader, &NetworkUdp::signalGotDatagrams,                            // Получение прочитанных из сокета данных с приведением к типу
            this, &AsynchSocketInterface::signalUdpGotRequest,                  // и пересылкой конечным получателям в главном потоке
            Qt::QueuedConnection);                                              // Обработка сигнала встает в общую очередь задач для избежания ошибок синхронизации
    connect(this, &AsynchSocketInterface::signalUdpPostResponse,
            loader, &NetworkUdp::sendDatagram,
            Qt::QueuedConnection);
    connect(loader, &NetworkUdp::signalRecvSocketOpen,
            this, &AsynchSocketInterface::signalRecvSocketOpen,
            Qt::QueuedConnection);
    connect(loader, &NetworkUdp::signalSendSocketOpen,
            this, &AsynchSocketInterface::signalSendSocketOpen,
            Qt::QueuedConnection);
    connect(this, &AsynchSocketInterface::synchSendUDP,
            loader, &NetworkUdp::sendDatagram,
            Qt::DirectConnection);

    NetworkTcpServer* tcp_server = new NetworkTcpServer();
    tcp_server->moveToThread(&m_thread);

    connect(&m_thread, &QThread::finished,
            tcp_server, &NetworkTcpServer::deleteLater);
    connect(this, &AsynchSocketInterface::__killSockets__,
            tcp_server, &NetworkTcpServer::__killSockets__);

    m_thread.start();

    connect(this, &AsynchSocketInterface::signalBindRecvSocket,                 // Подключение сигналов, управляющих сокетом, происходит
            loader, &NetworkUdp::initRecvSocket,                                // после запуска параллельного потока
            Qt::QueuedConnection);                                              //
    connect(this, &AsynchSocketInterface::signalBindSendSocket,                 // Подключение сигналов, управляющих сокетом, происходит
            loader, &NetworkUdp::initSendSocket,                                // после запуска параллельного потока
            Qt::QueuedConnection);
    connect(this, &AsynchSocketInterface::signalUnbindRecvSocket,               //
            loader, &NetworkUdp::disconnectRecvSocket,                          //
            Qt::QueuedConnection);                                              //
    connect(this, &AsynchSocketInterface::signalUnbindSendSocket,               //
            loader, &NetworkUdp::disconnectSendSocket,                          //
            Qt::QueuedConnection);
    connect(this, &AsynchSocketInterface::signalEnableLoading,                  //
            loader, &NetworkUdp::enableReading);
    connect(tcp_server, &NetworkTcpServer::signalGotRequest,
            this, &AsynchSocketInterface::signalTcpGotRequest,
            Qt::QueuedConnection);
    connect(this, &AsynchSocketInterface::signalTcpPostResponse,
            tcp_server, &NetworkTcpServer::postResponse,
            Qt::QueuedConnection);
    connect(this, &AsynchSocketInterface::signalEnableTcpServer,
            tcp_server, &NetworkTcpServer::initServer);
    connect(this, &AsynchSocketInterface::signalDisableTcpServer,
            tcp_server, &NetworkTcpServer::shutDownServer);
    connect(tcp_server, &NetworkTcpServer::signalServerOpen,
            this, &AsynchSocketInterface::signalServerOpen);
}
/*
 *
 */
MLB::AsynchSocketInterface::~AsynchSocketInterface()
{
    emit __killSockets__();
    if (m_thread.wait(QDeadlineTimer(INT64_MAX)))
        return;

    m_thread.quit();
    if (m_thread.wait(QDeadlineTimer(INT64_MAX)))
        return;

    qDebug() << "Netwok's thread entered deadlock on exit";
}

//------------------------------------------------------------------------------
/*
 *
 */
qsizetype MLB::NetworkUdp::processPendingDatagrams()
{
    const QByteArray data = readPendingDatagrams();

    if (!data.isEmpty())
        emit signalGotDatagrams(data);

    return data.size();
}
/*
 *
 */
bool MLB::NetworkUdp::initRecvSocket(int portRecv, QString address)
{
    if (portRecv == 0)
    {
        qDebug() << "[" << udp_client_name << "]"
                 << msg_zero_port;
        return false;
    }

    disconnectRecvSocket();

    if (m_udpRecvSocket != nullptr)
        delete m_udpRecvSocket;

    m_udpRecvSocket = new QUdpSocket();

    bool state{ true };

    if (m_udpRecvSocket->bind(QHostAddress(address), portRecv, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
    {
        qDebug() << "[" << udp_client_name << "]"
                 << address+":"+QString::number(portRecv).toStdString().c_str()
                 << msg_bind_success;

        if (address == multicast_group_address)
        {
            if (m_udpRecvSocket->joinMulticastGroup(QHostAddress(address)))
            {
                qDebug() << "[" << udp_client_name << "]"
                         << "Adress ="<< address << msg_MG_join_success;
            }
            else
            {
                state = false;
                qDebug() << "[" << udp_client_name << "]"
                         << "Adress ="<< address << msg_MG_join_fail
                         << m_udpRecvSocket->bind(QHostAddress::AnyIPv4, portRecv, QUdpSocket::ShareAddress);
            }
        }
    }
    else
    {
        state = false;
        qDebug() << "[" << udp_client_name << "]"
                 << address+":"+QString::number(portRecv).toStdString().c_str()
                 << msg_bind_fail;
    }

    if (!state)
        m_udpRecvSocket->close();

    emit signalRecvSocketOpen(state);
    return state;
}
/*
 *
 */
void MLB::NetworkUdp::disconnectRecvSocket()
{
    if (m_udpRecvSocket != nullptr)
    {
        m_udpRecvSocket->close();
        qDebug() << "[" << udp_client_name << "]"
                 << msg_closed;
        emit signalRecvSocketOpen(false);
    }
}
/*
 *
 */
void MLB::NetworkUdp::enableReading(bool state)
{
    if (m_udpRecvSocket == nullptr ||
        m_udpRecvSocket->state() != QAbstractSocket::BoundState)
    {
        if (state)
            qDebug() << "[" << udp_client_name << "]"
                     << "Can't read from uninitialized or closed socket";
        return;
    }

    if (state)
    {
        connect(m_udpRecvSocket, &QUdpSocket::channelReadyRead,
                this, &NetworkUdp::processPendingDatagrams);
        if (m_udpRecvSocket->hasPendingDatagrams())
            processPendingDatagrams();
    }
    else
        disconnect(m_udpRecvSocket, &QUdpSocket::channelReadyRead,
                   this, &NetworkUdp::processPendingDatagrams);
}
/*
 *
 */
bool MLB::NetworkUdp::initSendSocket(int portSend, QString address)
{
    if (portSend == 0)
    {
        qDebug() << "[" << udp_server_name << "]"
                 << msg_zero_port;
        return false;
    }

    disconnectSendSocket();

    if (m_udpSendSocket != nullptr)
        delete m_udpSendSocket;

    m_udpSendSocket = new QUdpSocket();

    bool state{ true };

    if (m_udpSendSocket->bind(QHostAddress(QHostAddress::AnyIPv4), -1, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
    {
        qDebug() << "[" << udp_server_name << "]"
                 << (m_udpSendSocket->localAddress().toString()+":"
                    +QString::number(m_udpSendSocket->localPort())).toStdString().c_str()
                 << msg_bind_success;

//        if (address == multicast_group_address)
//        {
//            if (m_udpSendSocket->joinMulticastGroup(QHostAddress(address)))
//            {
//                qDebug() << "[" << udp_server_name << "]"
//                         << "Adress ="<< address << msg_MG_join_success;
//            }
//            else
//            {
//                state = false;
//                qDebug() << "[" << udp_server_name << "]"
//                         << msg_MG_join_fail;
//            }
//        }

        m_addressToSend = address;
        m_portToSend = portSend;
    }
    else
    {
        state = false;
        qDebug() << "[" << udp_server_name << "]"
                 << msg_bind_fail
                 << m_udpSendSocket->bind(QHostAddress(QHostAddress::AnyIPv4), -1);
    }

    if (!state)
        m_udpSendSocket->close();

    emit signalSendSocketOpen(state);
    return state;
}
/*
 *
 */
void MLB::NetworkUdp::disconnectSendSocket()
{
    if (m_udpSendSocket != nullptr)
    {
        m_udpSendSocket->close();
        qDebug() << "[" << udp_server_name << "]"
                 << (m_udpSendSocket->localAddress().toString()+":"
                    +QString::number(m_udpSendSocket->localPort())).toStdString().c_str()
                 << msg_closed;
        emit signalSendSocketOpen(false);
    }
}
/*
 *
 */
qint64 MLB::NetworkUdp::sendDatagram(QByteArray data)
{
    return m_udpSendSocket->writeDatagram(
                data,
                QHostAddress(m_addressToSend),
                m_portToSend
            );
}
/*
 *
 */
void MLB::NetworkUdp::__killSockets__()
{
    enableReading(false);
    disconnectRecvSocket();
    disconnectSendSocket();
    __killNetworkThread__();
}
/*
 *
 */
MLB::NetworkUdp::NetworkUdp(QObject *parent, int portSend, int portRecv, QString address)
    : NetworkBase(parent), m_addressToSend(address)
{
    if (parent != nullptr)
    {
        initRecvSocket(portRecv);
        initSendSocket(portSend, address);
        enableReading(true);
    }
}
/*
 *
 */
MLB::NetworkUdp::~NetworkUdp()
{
    if (m_udpRecvSocket != nullptr)
        delete m_udpRecvSocket;

    if (m_udpSendSocket != nullptr)
        delete m_udpSendSocket;
}
/*
 *
 */
QByteArray MLB::NetworkUdp::readPendingDatagrams()
{
    if (m_udpRecvSocket == nullptr || !m_udpRecvSocket->isValid())
    {
        emit signalRecvSocketOpen(false);
        return QByteArray();
    }

    if (m_udpRecvSocket->hasPendingDatagrams())
        return m_udpRecvSocket->receiveDatagram(sizeof(char)*65535).data();

    return QByteArray();
}

//------------------------------------------------------------------------------
/*
 *
 */
MLB::NetworkTcpServer::NetworkTcpServer(QObject *parent, int port)
    : NetworkBase(parent)
{
    if (parent != nullptr)
        initServer(port);
}
/*
 *
 */
MLB::NetworkTcpServer::~NetworkTcpServer()
{
    if (mTcpServer != nullptr)
    {
        mTcpServer->close();
        delete mTcpServer;
        qDebug() << "[" << tcp_server_name << "]"
                 << msg_closed;
    }
}
/*
 *
 */
void MLB::NetworkTcpServer::postResponse(QByteArray data, QMetaType type, QTcpSocket* recipient)
{
    ResponsePtr shared_buf = std::make_shared<QByteArray>(data);

    emit signalGotResponse(shared_buf, type, recipient);
}
/*
 *
 */
void MLB::NetworkTcpServer::initServer(int port)
{
    bool state{ false };

    if (mTcpServer == nullptr)
    {
        mTcpServer = new QTcpServer();
        state = mTcpServer->listen(QHostAddress::Any, port);

        if (state)
        {
            qDebug() << "[" << tcp_server_name << "]"
                     << "Port =" << port << msg_start_success;
            connect(mTcpServer, &QTcpServer::newConnection, this, &NetworkTcpServer::slotNewConnection);

            emit signalServerOpen(true);
        }
        else
        {
            qDebug() << "[" << tcp_server_name << "]"
                     << "Port =" << port << msg_start_fail;
            shutDownServer();
        }
    }
}
/*
 *
 */
void MLB::NetworkTcpServer::shutDownServer()
{
    if (mTcpServer != nullptr)
    {
        mTcpServer->close();
        delete mTcpServer;
        mTcpServer = nullptr;
        qDebug() << "[" << tcp_server_name << "]"
                 << msg_closed;
    }

    emit signalServerOpen(false);
}
/*
 *
 */
void MLB::NetworkTcpServer::__killSockets__()
{
    shutDownServer();
    __killNetworkThread__();
}
/*
 *
 */
void MLB::NetworkTcpServer::slotNewConnection()
{
    QTcpSocket* new_socket = mTcpServer->nextPendingConnection();

    if (new_socket->isOpen())
        qDebug() << "[" << tcp_socket_name << "]"
                 << "Adress =" << new_socket->peerAddress().toString().toStdString().c_str()
                 << "Port =" << new_socket->localPort() << msg_connect_success;
    else
    {
        qDebug() << "[" << tcp_socket_name << "]"
                 << "Adress =" << new_socket->peerAddress().toString().toStdString().c_str()
                 << "Port =" << new_socket->localPort() << msg_connect_fail;
        return;
    }

    new_socket->setParent(nullptr);
    NetworkTcpSession* socket_core = new NetworkTcpSession();
    socket_core->setListenedSocket(new_socket);
    QThread* new_thread = new QThread();
    new_socket->moveToThread(new_thread);
    socket_core->moveToThread(new_thread);

    connect(mTcpServer, &QTcpServer::destroyed,
            new_socket, &QTcpSocket::disconnectFromHost);
    connect(new_socket, &QTcpSocket::disconnected,
            socket_core, &NetworkTcpSession::deleteLater);
    connect(socket_core, &NetworkTcpSession::destroyed,
            new_thread, &QThread::quit);
    connect(new_thread, &QThread::finished,
            new_thread, &QThread::deleteLater);

    new_thread->start();

    connect(this, &NetworkTcpServer::signalGotResponse,
            socket_core, &NetworkTcpSession::sendResponse,
            Qt::QueuedConnection);
    connect(socket_core, &NetworkTcpSession::signalGotRequest,
            this, &NetworkTcpServer::signalGotRequest,
            Qt::QueuedConnection);
}

//------------------------------------------------------------------------------
/*
 *
 */
void MLB::NetworkTcpSession::sendResponse(ResponsePtr data, QMetaType type, QTcpSocket* recipient)
{
    if (recipient != m_socket || m_socket == nullptr)
        return;

    const int max_packet_size{ 65535 };

    if (max_packet_size < data->size())
    {
        if (type.sizeOf() < max_packet_size && type.sizeOf() != 0)
        {
            const int send_delay_ms{ 1 };
            const int num_packets = data->size() / type.sizeOf();

            for (int i = 0; i < num_packets; ++i)
            {
                sendDatagram(data->sliced(i * type.sizeOf(), type.sizeOf()));
                QTest::qWait(send_delay_ms);
            }
        }
    }
    else
        sendDatagram(*(data.get()));
}
/*
 *
 */
void MLB::NetworkTcpSession::processRequest()
{
    QByteArray req = readPendingDatagrams();

    emit signalGotRequest(req, m_socket);
}
/*
 *
 */
MLB::NetworkTcpSession::NetworkTcpSession(QObject* parent)
    : NetworkBase(parent)
{

}
/*
 *
 */
MLB::NetworkTcpSession::~NetworkTcpSession()
{
    if (m_socket != nullptr)
    {
        qDebug() << "[" << tcp_socket_name << "]"
                 << msg_disconnected;

        m_socket->disconnectFromHost();
        delete m_socket;
    }
}
/*
 *
 */
QByteArray MLB::NetworkTcpSession::readPendingDatagrams()
{
    if (m_socket != nullptr)
        return m_socket->readAll();
    else
        return QByteArray();
}
/*
 *
 */
qint64 MLB::NetworkTcpSession::sendDatagram(QByteArray data)
{
    if (m_socket != nullptr)
        return m_socket->write(data);
    else
        return -1;
}
/*
 *
 */
void MLB::NetworkTcpSession::setListenedSocket(QTcpSocket* socket)
{
    if (socket == nullptr)
    {
        qDebug() << "[" << tcp_socket_name << "]"
                 << "can't associate socket with nullptr";
        return;
    }
    if (parent() != nullptr)
    {
        qDebug() << "[" << tcp_socket_name << "]"
                 << "can't associate session with parent to custom socket";
        return;
    }

    m_socket = socket;
    connect(socket, &QTcpSocket::readyRead,
            this, &NetworkTcpSession::processRequest);
}

//------------------------------------------------------------------------------
