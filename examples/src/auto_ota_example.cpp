#include "dot_util.h"
#include "RadioEvent.h"

#if ACTIVE_EXAMPLE == AUTO_OTA_EXAMPLE

mDot* dot = NULL;
lora::ChannelPlan* plan = NULL;

mbed::UnbufferedSerial pc(USBTX, USBRX);

int main() {
    // Custom event handler for automatically displaying RX data
    RadioEvent events;

    pc.baud(115200);

#if defined(TARGET_XDOT_L151CC)
    i2c.frequency(400000);
#endif

    mts::MTSLog::setLogLevel(mts::MTSLog::TRACE_LEVEL);

    logInfo("----- Running auto ota example -----");

    // Create channel plan
    plan = create_channel_plan();
    assert(plan);

    dot = mDot::getInstance(plan);
    assert(dot);

    // attach the custom events handler
    dot->setEvents(&events);

    // Enable FOTA for multicast support
    Fota::getInstance(dot);

    if (!dot->getStandbyFlag() && !dot->getPreserveSession()) {
        logInfo("mbed-os library version: %d.%d.%d", MBED_MAJOR_VERSION, MBED_MINOR_VERSION, MBED_PATCH_VERSION);

        // start from a well-known state
        logInfo("defaulting Dot configuration");
        dot->resetConfig();
        dot->resetNetworkSession();

        // make sure library logging is turned on
        dot->setLogLevel(mts::MTSLog::INFO_LEVEL);

        // update configuration if necessary
        // in AUTO_OTA mode the session is automatically saved, so saveNetworkSession and restoreNetworkSession are not needed
        if (dot->getJoinMode() != mDot::AUTO_OTA) {
            logInfo("changing network join mode to AUTO_OTA");
            if (dot->setJoinMode(mDot::AUTO_OTA) != mDot::MDOT_OK) {
                logError("failed to set network join mode to AUTO_OTA");
            }
        }

        // To preserve session over power-off or reset enable this flag
        // dot->setPreserveSession(true);

        // in OTA and AUTO_OTA join modes, the credentials can be passed to the library as a name and passphrase or an ID and KEY
        // only one method or the other should be used!
        // network ID = crc64(network name)
        // network KEY = cmac(network passphrase)
#if defined(DERIVE_FROM_TEXT)
        update_ota_config_name_phrase(cfg::network_name, cfg::network_passphrase, cfg::frequency_sub_band, cfg::network_type, cfg::ack);
#else
        update_ota_config_id_key(cfg::network_id, cfg::network_key, cfg::frequency_sub_band, cfg::network_type, cfg::ack);
#endif

        // configure network link checks
        // network link checks are a good alternative to requiring the gateway to ACK every packet and should allow a single gateway to handle more Dots
        // check the link every count packets
        // declare the Dot disconnected after threshold failed link checks
        // for count = 3 and threshold = 5, the Dot will ask for a link check response every 5 packets and will consider the connection lost if it fails to receive 3 responses in a row
        update_network_link_check_config(3, 5);

        // enable or disable Adaptive Data Rate
        dot->setAdr(cfg::adr);

        // Configure the join delay
        dot->setJoinDelay(cfg::join_delay);

        // save changes to configuration
        logInfo("saving configuration");
        if (!dot->saveConfig()) {
            logError("failed to save configuration");
        }

        // display configuration
        display_config();
    }

    while (true) {
        // Defensive programming in case the gateway/network server continuously gives a reason to send.
        static const uint8_t max_consecutive_sends = 4;
        static uint8_t consecutive_sends = max_consecutive_sends;
        static uint8_t payload_size_sent;

        // join network if not joined
        if (!dot->getNetworkJoinStatus()) {
            join_network();
        }

        // If the channel plan has duty cycle restrictions, wait may be required.
        dot_wait_for_channel();

        if(send(payload_size_sent) == mDot::MDOT_OK) {
            // In class A mode, downlinks only occur following an uplink. So process downlinks after a successful send.
            if (events.PacketReceived && (events.RxPort == (dot->getAppPort()))) {
                std::vector<uint8_t> rx_data;
                if (dot->recv(rx_data) == mDot::MDOT_OK) {
                    logInfo("Downlink data (port %d) %s", dot->getAppPort(),mts::Text::bin2hexString(rx_data.data(), rx_data.size()).c_str());
                }
            }
            // Optional reasons to send again right away.
            // 1. Data pending. There are downlinks queued up for this endpoint. This reason is cleared on send and updated on reception
            //    of a downlink. So, a missed downlink results in no data pending.
            // 2. There are MAC command answers pending.
            // 3. An Ack has been requested of this endpoint.
            // 4. Sent an empty payload to clear MAC commands. dot->hasMacCommands is not true now but that's because an 
            //    empty packet was sent making room for the actual payload to be sent.
            if ((dot->getDataPending() || dot->hasMacCommands() || dot->getAckRequested() || (payload_size_sent == 0)) && consecutive_sends > 1) {
                // Don't sleep... send again. 
                consecutive_sends--;
                if(dot->getDataPending())
                    logInfo("Data pending... send again");
                if(dot->hasMacCommands())
                    logInfo("Respond with MAC answers... send again");
                if(dot->getAckRequested())
                    logInfo("Ack has been requested... send again");
                if(payload_size_sent == 0)
                    logInfo("Sent an empty payload to clear MAC commands... send again");
            } else {
                consecutive_sends = max_consecutive_sends;
                dot_sleep();
            }
        } else { // Send failed. Don't drain battery by repeatedly sending.
            dot_sleep();
        }
    }

    return 0;
}

#endif
