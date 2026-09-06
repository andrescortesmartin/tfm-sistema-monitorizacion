var out = {
    gw_prev_uplink_count: msg.gw_uplink_count,
    gw_prev_poll_ts: Date.now()
};
return {msg: out, metadata: metadata, msgType: "POST_ATTRIBUTES_REQUEST"};