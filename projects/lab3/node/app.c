
void receive_packet(node_id_t sender, uint8_t *payload, uint8_t len)
{
    nrk_time_t ping_processing_time = {2, 0};
    app_pkt_type_t type;

    type = payload[APP_PKT_TYPE_OFFSET];

    nrk_kprintf(PSTR("Received app pkt: "));
    printf("sdr %d type %d\r\n", sender, app_pkt_type_to_string(type));

    nrk_led_set(LED_PROCESSING_APP_PKT);

    switch (type) {
        case APP_PKT_TYPE_PING:
            nrk_kprintf(PSTR("Pinged by msg\r\n"));
            nrk_wait(ping_processing_time);
            break;
        default:
            nrk_kprintf(PSTR("WARN: unknown app pkt type\r\n"));
    }

    nrk_led_clr(LED_PROCESSING_APP_PKT);
}

