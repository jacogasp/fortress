//
// Created by Jacopo Gasparetto on 19/05/21.
//
// SPDX-FileCopyrightText: 2021 INFN
// SPDX-License-Identifier: EUPL-1.2
// SPDX-FileContributor: Jacopo Gasparetto
//

#ifndef FORTRESS_THREADSAFE_QUEUE_H
#define FORTRESS_THREADSAFE_QUEUE_H

#include "commons.h"
#include "message.h"

namespace fortress::net {
    template<typename T>
    class ts_queue {

    protected:
        std::mutex m_muxQueue;
        std::deque<T> m_deque;

        std::condition_variable m_cvBlocking;
        std::mutex m_muxCvBlocking;

        bool m_bForceAwake = false;

    public:
        ts_queue() = default;

        // Prevent the queue to be copied because probably it already changed in the thread
        ts_queue(const ts_queue<T> &) = delete;

        virtual ~ts_queue() { clear(); }

        const T& front() {
            std::scoped_lock lock(m_muxQueue);
            return m_deque.front();
        }

        const T& back() {
            std::scoped_lock lock(m_muxQueue);
            return m_deque.back();
        }

        T pop_front() {
            std::scoped_lock lock(m_muxQueue);
            auto t = std::move(m_deque.front());     // Cache the item
            m_deque.pop_front();
            return t;
        }

        T pop_back() {
            std::scoped_lock lock(m_muxQueue);
            auto t = std::move(m_deque.back());     // Cache the item
            m_deque.pop_back();
            return t;
        }

        void push_front(const T& item) {
            std::scoped_lock lock(m_muxQueue);
            m_deque.template emplace_front(std::move(item));

            // Wake the wait() function notifying that the queue is not empty anymore
            std::unique_lock<std::mutex> ul(m_muxCvBlocking);
            m_cvBlocking.notify_one();
        }

        void push_back(const T& item) {
            std::scoped_lock lock(m_muxQueue);
            m_deque.template emplace_back(std::move(item));

            // Wake the wait() function notifying that the queue is not empty anymore
            std::unique_lock<std::mutex> ul(m_muxCvBlocking);
            m_cvBlocking.notify_one();
        }

        bool empty() {
            std::scoped_lock lock(m_muxQueue);
            return m_deque.empty();
        }

        size_t count() {
            std::scoped_lock lock(m_muxQueue);
            return m_deque.size();
        }

        void clear() {
            std::scoped_lock lock(m_muxQueue);
            m_deque.clear();
        }

        // TODO: the following two methods can be safely removed

        void wait() {
            // Checks whether the queue is empty or not.
            while (empty() && !m_bForceAwake) {
                // Send the thread to sleep. Wait here until something signals the conditional variable to wake up.
                std::unique_lock<std::mutex> ul(m_muxCvBlocking);
                m_cvBlocking.wait(ul);
            }

            // When receive the notification, release control to the calling function
            m_bForceAwake = false;
        }

        void stopWaiting() {
            // Wake the wait() function notifying that the queue is not empty anymore
            std::unique_lock<std::mutex> ul(m_muxCvBlocking);
            m_bForceAwake = true;
            m_cvBlocking.notify_one();
        }
    };
}

#endif //FORTRESS_THREADSAFE_QUEUE_H
