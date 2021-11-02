//
// Created by Jacopo Gasparetto on 16/05/21.
//
// SPDX-FileCopyrightText: 2021 INFN
// SPDX-License-Identifier: EUPL-1.2
// SPDX-FileContributor: Jacopo Gasparetto
//

#include "Backend.h"

Backend::Backend(QObject *parent)
        : QObject{ parent },
          client_interface{ m_context },
          m_pPingTimer{ std::make_unique<asio::steady_timer>(m_context, PING_DELAY) } {

    using namespace fortress::consts;
    generatePlotSeries(N_CHANNELS, WINDOW_SIZE_IN_POINT);
    m_file.setAutoRemove(true);
    std::cout << "Instantiated backend helper\n";
}

Backend::~Backend() {
    m_pPingTimer->cancel();
    disconnectFromHost();
    m_threadContext.join();
    closeFile();
}


bool Backend::connectToHost(const QString &host, uint16_t port) {
    bool bConnectionSuccessful = connect(host.toStdString(), port);
    m_threadContext = std::thread([&]() { m_context.run(); });
    return bConnectionSuccessful;
}

void Backend::disconnectFromHost() {
    if (isConnected()) {
        message<MsgTypes> disconnectMsg;
        disconnectMsg.header.id = ClientDisconnect;
        sendMessage(disconnectMsg);
    }

    if (m_bIsPinging)
        togglePingUpdate();

    bool prevIsConnected{ isConnected() };
    client_interface::disconnect();

    if (prevIsConnected != isConnected())
            emit connectionStatusChanged(isConnected());

    if (!isConnected()) {
        std::cout << "[BACKEND] Disconnected from host" << std::endl;
    }
}


void Backend::clearData() {
    for (auto &ch: m_data)
        ch.clear();

    m_data.clear();
    m_t = m_data_idx = 0;
    m_chIntegralValues = { 0.0, 0.0, 0.0, 0.0 };
    m_chLastValues = { 0.0, 0.0, 0.0, 0.0 };
    generatePlotSeries(fortress::consts::N_CHANNELS, fortress::consts::WINDOW_SIZE_IN_POINT);
}

// Callbacks

//void Backend::onConnectionFailed(std::error_code &ec) {
//    stopListening();
//    emit connectionFailed(QString::fromStdString(ec.message()));
//}

void Backend::onMessage(message<MsgTypes> &msg) {
    switch (msg.header.id) {
        case MsgTypes::ServerAccept: {
            std::cout << "[BACKEND] Server accepted\n";
            emit connectionStatusChanged(true);
            break;
        }

        case MsgTypes::ServerMessage: {
            std::cout << "[BACKEND] Message from server: " << msg << ": ";
            std::ostringstream out;

            for (auto ch: msg.body)
                out << ch;

            std::cout << out.str() << std::endl;
            break;
        }

        case MsgTypes::ServerPing: {
            std::chrono::system_clock::time_point timeNow = std::chrono::system_clock::now();
            std::chrono::system_clock::time_point timeThen;

            msg >> timeThen;
            m_lastPingValue = std::chrono::duration<double>(timeNow - timeThen).count() * 1000;
            break;
        }

        case MsgTypes::ClientPing: {
            sendMessage(msg);
            break;
        }

        case MsgTypes::ServerReadings: {
            onReadingsReceived(msg);
            break;
        }

        default:
            std::cout << "???\n";
    }
}

// Helpers

void Backend::onReadingsReceived(message<MsgTypes> &msg) {
    msg >> m_chLastValues[0];
    msg >> m_chLastValues[1];
    msg >> m_chLastValues[2];
    msg >> m_chLastValues[3];

    for (int i = 0; i < fortress::consts::N_CHANNELS; ++i) {
        if (m_chLastValues[i] > m_chMaxValues[i]) m_chMaxValues[i] = m_chLastValues[i];

        m_textStream << m_chLastValues[i];

        if (i < fortress::consts::N_CHANNELS - 1)
            m_textStream << ',';
    }
    m_textStream << '\n';

    addPointsToSeries(m_chLastValues);
}

void Backend::pingHandler() {
    if (m_bIsPinging) {
        message<MsgTypes> pingMsg;
        pingMsg.header.id = ServerPing;
        pingMsg << std::chrono::system_clock::now();

        sendMessage(pingMsg);

        m_pPingTimer->expires_from_now(PING_DELAY);
        m_pPingTimer->async_wait([this](asio::error_code ec) {
            pingHandler();
        });
    }
}

void Backend::togglePingUpdate() {
    if (m_bIsPinging) {
        m_bIsPinging = false;
    } else {
        m_bIsPinging = true;
        pingHandler();
    }
}

void Backend::openFile() {
    m_file.open();
    m_textStream << "channel1,channel2,channel3,channel4\n";
}

void Backend::closeFile() {
    if (m_file.isOpen())
        m_file.close();

    m_textStream.flush();
    m_textStream.reset();
}

double Backend::getLastPingValue() const {
    return m_lastPingValue;
}

// Slots
void Backend::generatePlotSeries(int n_channels, int length) {

    for (int ch = 0; ch < n_channels; ++ch) {
        QList<QPointF> points{};
        points.reserve(length);

        for (int i = 0; i < length; ++i)
            points.push_front(QPointF{});

        m_data.append(points);
    }
}

void Backend::addPointsToSeries(const std::array<double, fortress::consts::N_CHANNELS> &values) {
    using namespace fortress::consts;

    m_data_idx = m_t % WINDOW_SIZE_IN_POINT;

    for (int ch = 0; ch < N_CHANNELS; ++ch) {

        auto chSeries = &m_data[ch];

        double x{ static_cast<double>(m_data_idx % (WINDOW_SIZE_IN_POINT)) };
        double y{ values[ch] };
        chSeries->replace(m_data_idx, QPointF{ x, y });

        auto max = *std::max_element(chSeries->begin(), chSeries->end(), [](const QPointF &p1, const QPointF &p2) {
            return p1.y() < p2.y();
        });

        m_chMaxValues[ch] = max.y();
        m_chIntegralValues[ch] += values[ch];
    }
    ++m_t;
}

void Backend::updatePlotSeries(QAbstractSeries *qtQuickLeftSeries, QAbstractSeries *qtQuickRightSeries, int channel) {

    if (qtQuickLeftSeries && qtQuickRightSeries) {
        auto *xyQtQuickLeftSeries = dynamic_cast<QXYSeries *>(qtQuickLeftSeries);
        auto *xyQtQuickRightSeries = dynamic_cast<QXYSeries *>(qtQuickRightSeries);
        auto channelData = &m_data[channel];

        auto leftSeries = QList(channelData->begin(), channelData->begin() + m_data_idx);
        xyQtQuickLeftSeries->replace(leftSeries);

        // FIXME: to prevent glitches the first time the we span from left to right (right series is empty) we use
        // as workaround m_data_idx + 1, thus we need a sanity check to prevent out of bound. This check is heavy and
        // should be removed
        if (m_data_idx < fortress::consts::WINDOW_SIZE_IN_POINT - 2) {
            auto rightSeries = QList(channelData->begin() + m_data_idx + 1, channelData->end());
            xyQtQuickRightSeries->replace(rightSeries);
        }
    }
}

// Accessors

QList<QPointF> Backend::getSeries() const {
    return m_data.at(0);
}

int Backend::windowSize() {
    return fortress::consts::WINDOW_SIZE_IN_POINT;
}

double Backend::getLastChannelValue(int channel) const {
    return m_chLastValues[channel];
}

double Backend::getMaxChannelValue(int channel) const {
    return m_chMaxValues[channel];
}

double Backend::getIntegralChannelValue(uint8_t channel) const {
    return m_chIntegralValues[channel];
}

void Backend::sendStartUpdateCommand(uint16_t frequency) {
    openFile();
    message<MsgTypes> msg;
    msg.header.id = ClientStartUpdating;
    msg << frequency;
    sendMessage(msg);
}

void Backend::sendStopUpdateCommand() {
    closeFile();
    message<MsgTypes> msg;
    msg.header.id = ClientStopUpdating;
    sendMessage(msg);
}

void Backend::saveFile(QUrl &destinationPath) {
    QFile::copy(m_file.fileName(), destinationPath.path());
}