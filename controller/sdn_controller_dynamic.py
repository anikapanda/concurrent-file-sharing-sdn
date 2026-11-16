"""OpenFlow 1.3 learning switch with automatic high-traffic mitigation.

Designed for the project's Mininet single,3 topology:
  s1-eth1 -> h1, s1-eth2 -> h2, s1-eth3 -> h3.
Traffic above RATE_LIMIT_BPS on a client ingress port triggers a temporary
OpenFlow drop rule. This is a traffic-volume policy, not congestion inference.
"""
import time

from os_ken.base import app_manager
from os_ken.controller import ofp_event
from os_ken.controller.handler import CONFIG_DISPATCHER, MAIN_DISPATCHER, set_ev_cls
from os_ken.ofproto import ofproto_v1_3
from os_ken.lib import hub
from os_ken.lib.packet import packet, ethernet, ether_types

POLL_INTERVAL = 2
RATE_LIMIT_BPS = 1_000_000  # bytes per second, approx. 8 Mbit/s
BLOCK_SECONDS = 10
CLIENT_PORTS = {1, 2}  # verify with Mininet `net` before testing


class FileSharingController(app_manager.OSKenApp):
    OFP_VERSIONS = [ofproto_v1_3.OFP_VERSION]

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.mac_to_port = {}
        self.datapaths = {}
        self.last_stats = {}
        self.blocked_until = {}
        self.monitor_thread = hub.spawn(self._monitor)
        self.logger.info("File Sharing SDN Controller with traffic monitoring started")

    def add_flow(self, datapath, priority, match, actions, hard_timeout=0):
        parser = datapath.ofproto_parser
        ofproto = datapath.ofproto
        instructions = [parser.OFPInstructionActions(ofproto.OFPIT_APPLY_ACTIONS, actions)]
        mod = parser.OFPFlowMod(datapath=datapath, priority=priority,
                                match=match, instructions=instructions,
                                hard_timeout=hard_timeout)
        datapath.send_msg(mod)

    @set_ev_cls(ofp_event.EventOFPSwitchFeatures, CONFIG_DISPATCHER)
    def switch_features_handler(self, ev):
        datapath = ev.msg.datapath
        parser = datapath.ofproto_parser
        ofproto = datapath.ofproto
        self.datapaths[datapath.id] = datapath
        self.last_stats = {k: v for k, v in self.last_stats.items() if k[0] != datapath.id}
        actions = [parser.OFPActionOutput(ofproto.OFPP_CONTROLLER,
                                          ofproto.OFPCML_NO_BUFFER)]
        self.add_flow(datapath, 0, parser.OFPMatch(), actions)
        self.logger.info("Forwarding rule installed on switch %s", datapath.id)

    def _monitor(self):
        while True:
            for datapath in list(self.datapaths.values()):
                try:
                    parser = datapath.ofproto_parser
                    ofproto = datapath.ofproto
                    req = parser.OFPPortStatsRequest(datapath, 0, ofproto.OFPP_ANY)
                    datapath.send_msg(req)
                except Exception as exc:
                    self.logger.warning("Stats request failed: %s", exc)
            hub.sleep(POLL_INTERVAL)

    @set_ev_cls(ofp_event.EventOFPPortStatsReply, MAIN_DISPATCHER)
    def port_stats_reply_handler(self, ev):
        datapath = ev.msg.datapath
        now = time.monotonic()
        for stat in ev.msg.body:
            port = stat.port_no
            if port not in CLIENT_PORTS:
                continue
            key = (datapath.id, port)
            previous = self.last_stats.get(key)
            self.last_stats[key] = (stat.rx_bytes, now)
            if previous is None:
                continue
            previous_bytes, previous_time = previous
            elapsed = now - previous_time
            delta = stat.rx_bytes - previous_bytes
            if elapsed <= 0 or delta < 0:
                continue
            rate = delta / elapsed
            if rate <= RATE_LIMIT_BPS or now < self.blocked_until.get(key, 0):
                continue
            parser = datapath.ofproto_parser
            self.add_flow(datapath, 100, parser.OFPMatch(in_port=port), [],
                          hard_timeout=BLOCK_SECONDS)
            self.blocked_until[key] = now + BLOCK_SECONDS
            self.logger.warning(
                "HIGH TRAFFIC: switch=%s client_port=%s rate=%.2f Mbit/s; "
                "blocking ingress for %ss", datapath.id, port,
                rate * 8 / 1_000_000, BLOCK_SECONDS)

    @set_ev_cls(ofp_event.EventOFPPacketIn, MAIN_DISPATCHER)
    def packet_in_handler(self, ev):
        msg = ev.msg
        datapath = msg.datapath
        ofproto = datapath.ofproto
        parser = datapath.ofproto_parser
        in_port = msg.match['in_port']
        pkt = packet.Packet(msg.data)
        eth = pkt.get_protocol(ethernet.ethernet)
        if eth is None or eth.ethertype == ether_types.ETH_TYPE_LLDP:
            return
        src, dst, dpid = eth.src, eth.dst, datapath.id
        self.mac_to_port.setdefault(dpid, {})
        self.mac_to_port[dpid][src] = in_port
        out_port = self.mac_to_port[dpid].get(dst, ofproto.OFPP_FLOOD)
        actions = [parser.OFPActionOutput(out_port)]
        if out_port != ofproto.OFPP_FLOOD:
            match = parser.OFPMatch(in_port=in_port, eth_src=src, eth_dst=dst)
            self.add_flow(datapath, 1, match, actions)
        data = None if msg.buffer_id != ofproto.OFP_NO_BUFFER else msg.data
        out = parser.OFPPacketOut(datapath=datapath, buffer_id=msg.buffer_id,
                                  in_port=in_port, actions=actions, data=data)
        datapath.send_msg(out)
