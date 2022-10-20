// Copyright (C) 2014-2017 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef VSOMEIP_ENABLE_SIGNAL_HANDLING
#include <csignal>
#endif
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#include <vsomeip/vsomeip.hpp>

#include "sample-ids.hpp"

class WiperClient {
public:
    WiperClient(bool _use_tcp) :
            app_(vsomeip::runtime::get()->create_application()), use_tcp_(
                    _use_tcp) {
    }

    bool init() {
        if (!app_->init()) {
            std::cerr << "Couldn't initialize application" << std::endl;
            return false;
        }
        std::cout << "Client settings [protocol="
                << (use_tcp_ ? "TCP" : "UDP")
                << "]"
                << std::endl;

        app_->register_state_handler(
                std::bind(&WiperClient::on_state, this,
                        std::placeholders::_1));

        app_->register_message_handler(
                vsomeip::ANY_SERVICE, SAMPLE_INSTANCE_ID, vsomeip::ANY_METHOD,
                std::bind(&WiperClient::on_message, this,
                        std::placeholders::_1));

        app_->register_availability_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID,
                std::bind(&WiperClient::on_availability,
                          this,
                          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        std::set<vsomeip::eventgroup_t> its_groups;
        its_groups.insert(SAMPLE_EVENTGROUP_ID);
        app_->request_event(
                SAMPLE_SERVICE_ID,
                SAMPLE_INSTANCE_ID,
                SAMPLE_EVENT_ID,
                its_groups,
                vsomeip::event_type_e::ET_FIELD,
                (use_tcp_ ? vsomeip::reliability_type_e::RT_RELIABLE : vsomeip::reliability_type_e::RT_UNRELIABLE));
        app_->subscribe(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENTGROUP_ID);

        if (::getenv("ROUTE_DIAG")) {
            app_->set_routing_state(vsomeip_v3::routing_state_e::RS_DIAGNOSIS);
        }

        std::cout << "*** App"
            "[name:" << app_->get_name() <<
            ", cli:0x" << std::hex << app_->get_client() <<
            ", avail(0x" << std::hex << SAMPLE_SERVICE_ID << ", 0x" << std::hex << SAMPLE_INSTANCE_ID << "):"
                << app_->is_available(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID) <<
            ", routing:" << app_->is_routing() <<
            ", sec:" << (int)app_->get_security_mode() <<
            "]" << std::endl;

        return true;
    }

    void start() {
        app_->start();
    }

#ifndef VSOMEIP_ENABLE_SIGNAL_HANDLING
    /*
     * Handle signal to shutdown
     */
    void stop() {
        app_->clear_all_handler();
        app_->unsubscribe(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENTGROUP_ID);
        app_->release_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID);
        app_->release_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
        app_->stop();
    }
#endif

    void on_state(vsomeip::state_type_e _state) {
        std::cout << "Application " << app_->get_name() << " is "
        << (_state == vsomeip::state_type_e::ST_REGISTERED ?
                "registered." : "deregistered.") << std::endl;

        if (_state == vsomeip::state_type_e::ST_REGISTERED) {
            app_->request_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
        }
    }

    void on_availability(vsomeip::service_t _service, vsomeip::instance_t _instance, bool _is_available) {
        std::cout << "Service ["
                << std::setw(4) << std::setfill('0') << std::hex << _service << "." << _instance
                << "] is "
                << (_is_available ? "available." : "NOT available.")
                << std::endl;
    }

    void on_message(const std::shared_ptr<vsomeip::message> &_response) {
        std::stringstream its_message;
        its_message << "Received a notification for Event ["
                << std::setw(4)    << std::setfill('0') << std::hex
                << _response->get_service() << "."
                << std::setw(4) << std::setfill('0') << std::hex
                << _response->get_instance() << "."
                << std::setw(4) << std::setfill('0') << std::hex
                << _response->get_method() << "] to Client/Session ["
                << std::setw(4) << std::setfill('0') << std::hex
                << _response->get_client() << "/"
                << std::setw(4) << std::setfill('0') << std::hex
                << _response->get_session()
                << "] = ";
        std::shared_ptr<vsomeip::payload> its_payload =
                _response->get_payload();
        its_message << "(" << std::dec << its_payload->get_length() << ") ";
        for (uint32_t i = 0; i < its_payload->get_length(); ++i)
            its_message << std::hex << std::setw(2) << std::setfill('0')
                << (int) its_payload->get_data()[i] << " ";
        std::cout << its_message.str() << std::endl;

        if (_response->get_client() == 0) {
            if ((its_payload->get_length() % 5) == 0) {
                std::shared_ptr<vsomeip::message> its_get
                    = vsomeip::runtime::get()->create_request();
                its_get->set_service(SAMPLE_SERVICE_ID);
                its_get->set_instance(SAMPLE_INSTANCE_ID);
                its_get->set_method(SAMPLE_GET_METHOD_ID);
                its_get->set_reliable(use_tcp_);
                app_->send(its_get);
            }

            if ((its_payload->get_length() % 8) == 0) {
                std::shared_ptr<vsomeip::message> its_set
                    = vsomeip::runtime::get()->create_request();
                its_set->set_service(SAMPLE_SERVICE_ID);
                its_set->set_instance(SAMPLE_INSTANCE_ID);
                its_set->set_method(SAMPLE_SET_METHOD_ID);
                its_set->set_reliable(use_tcp_);

                const vsomeip::byte_t its_data[]
                    = { 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
                        0x48, 0x49, 0x50, 0x51, 0x52 };
                std::shared_ptr<vsomeip::payload> its_set_payload
                    = vsomeip::runtime::get()->create_payload();
                its_set_payload->set_data(its_data, sizeof(its_data));
                its_set->set_payload(its_set_payload);
                app_->send(its_set);
            }
        }
    }

private:
    std::shared_ptr< vsomeip::application > app_;
    bool use_tcp_;
};

#ifndef VSOMEIP_ENABLE_SIGNAL_HANDLING
    WiperClient *its_sample_ptr(nullptr);
    void handle_signal(int _signal) {
        if (its_sample_ptr != nullptr &&
                (_signal == SIGINT || _signal == SIGTERM))
            its_sample_ptr->stop();
    }
#endif

int main(int argc, char **argv) {
    bool use_tcp = false;

    std::string tcp_enable("--tcp");
    std::string udp_enable("--udp");

    int i = 1;
    while (i < argc) {
        if (tcp_enable == argv[i]) {
            use_tcp = true;
        } else if (udp_enable == argv[i]) {
            use_tcp = false;
        }
        i++;
    }

    WiperClient its_sample(use_tcp);
#ifndef VSOMEIP_ENABLE_SIGNAL_HANDLING
    its_sample_ptr = &its_sample;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
#endif
    if (its_sample.init()) {
        its_sample.start();
        return 0;
    } else {
        return 1;
    }
}