#ifndef NETWORK_H
#define NETWORK_H

#include <memory>
#include <QCoreApplication>
#include <QDebug>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QHostAddress>
#include <QVariant>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSemaphore>
#include <QTest>

namespace MLB {

//------------------------------------------------------------------------------

using ResponsePtr = std::shared_ptr<QByteArray>;

//------------------------------------------------------------------------------

namespace Network
{

static const char* udp_socket_name{ "UDP Socket" };
static const char* udp_server_name{ "UDP Server" };
static const char* udp_client_name{ "UDP Client" };
static const char* tcp_socket_name{ "TCP Socket" };
static const char* tcp_server_name{ "TCP Server" };

static const char* multicast_group_address{ "230.0.0.1" };
static const char* msg_zero_port{ "no port to send of change it from 0" };

static const char* msg_bind_success{ "is binded" };
static const char* msg_MG_join_success{ "is joined to multicast group" };
static const char* msg_start_success{ "is started" };
static const char* msg_connect_success{ "connected" };

static const char* msg_start_fail{ "starting falied" };
static const char* msg_bind_fail{ "bind failed" };
static const char* msg_MG_join_fail{ "failed to join multicast group"};
static const char* msg_connect_fail{ "failed to connect" };

static const char* msg_closed{ "closed" };
static const char* msg_disconnected{ "disconnected" };

// ~Network
}

//------------------------------------------------------------------------------

class NetworkBase : public QObject
{
    Q_OBJECT

public:
    NetworkBase(QObject* parent = nullptr);
    virtual ~NetworkBase() {}

    bool moveToThread(QThread* thread);

protected:
    using QObject::moveToThread;

    void __killNetworkThread__();

private:
    static QSemaphore m_threadKillSem;

};

//------------------------------------------------------------------------------
/*
 * Оболочка для сокета, работающего в параллельном потоке
 */
class AsynchSocketInterface : public QObject
{
    Q_OBJECT

signals:
    void signalUdpGotRequest(QByteArray data);                                  //
    void signalUdpPostResponse(QByteArray data);
    void signalSocketOpen(bool state);                                          // Состояние сокета
    void signalBindRecvSocket(int port, QString address);                       // Подключение сокета к указанному пору
    void signalUnbindRecvSocket();                                              // Отключение сокета от чтения порта
    void signalBindSendSocket(int port, QString address);                       // Подключение сокета к указанному пору
    void signalUnbindSendSocket();                                              // Отключение сокета от чтения порта
    void signalEnableLoading(bool state);                                       // Запуск чтения данных из сокета

    void signalEnableTcpServer(int port);
    void signalDisableTcpServer();
    void signalServerOpen(bool state);
    void signalTcpPostResponse(QByteArray data, QMetaType type, QTcpSocket* recipient);
    void signalTcpGotRequest(QByteArray data, QTcpSocket* sender);
    void __killSockets__();

public:
    AsynchSocketInterface (QObject* parent = nullptr);
    ~AsynchSocketInterface(                         );

private:
    QThread m_thread;

};

//------------------------------------------------------------------------------
/*
 * Управление сокетом
 */
class NetworkUdp : public NetworkBase
{
    Q_OBJECT

signals:
    void signalGotDatagrams(QByteArray data);                                   // Прочитанные данные
    void signalRecvSocketOpen(bool state);                                      // Состояние порта
    void signalSendSocketOpen(bool state);

public slots:
    qsizetype processPendingDatagrams();                                        // Проверка наличия входящих данных
    bool initRecvSocket(int portRecv, QString address = Network::multicast_group_address);           // Подключение сокета к порту
    void disconnectRecvSocket();                                                // Отключение сокета от порта
    void enableReading(bool state);

    bool initSendSocket(int portSend, QString address = "127.0.0.1");
    void disconnectSendSocket();
    qint64 sendDatagram(QByteArray data);
    
    void __killSockets__();

public:
    NetworkUdp(QObject *parent = nullptr, int portSend = 0, int portRecv = 0, QString address = Network::multicast_group_address);
    ~NetworkUdp() override;

    QByteArray readPendingDatagrams();

private:
    QUdpSocket* m_udpRecvSocket{ nullptr };
    QUdpSocket* m_udpSendSocket{ nullptr };
    QString m_addressToSend{ "" };
    int m_portToSend{ 0 };

};

//------------------------------------------------------------------------------

class NetworkTcpServer : public NetworkBase
{
    Q_OBJECT

signals:
    void signalGotRequest(QByteArray data, QTcpSocket* sender);
    void signalGotResponse(ResponsePtr data, QMetaType type, QTcpSocket* recipient);
    void signalServerOpen(bool state);

public:
    explicit NetworkTcpServer(QObject *parent = 0, int port = 0);
    ~NetworkTcpServer();

public slots:
    void postResponse(QByteArray data, QMetaType type, QTcpSocket* recipient);
    void initServer(int port);
    void shutDownServer();
    void __killSockets__();

private slots:
    void slotNewConnection();

private:
    QTcpServer * mTcpServer{ nullptr };

};

//------------------------------------------------------------------------------

class NetworkTcpSession : public NetworkBase
{
    Q_OBJECT

signals:
    void signalGotRequest(QByteArray data, QTcpSocket* sender);

public slots:
    void sendResponse(ResponsePtr data, QMetaType type, QTcpSocket* recipient);
    void processRequest();

public:
    explicit NetworkTcpSession(QObject* open_socket = nullptr);
    ~NetworkTcpSession();

    QByteArray readPendingDatagrams();
    qint64 sendDatagram(QByteArray data);
    void setListenedSocket(QTcpSocket* open_socket);

private:
    QTcpSocket* m_socket{ nullptr };

};

//------------------------------------------------------------------------------
// ~MLB
}

#endif // NETWORK_H
