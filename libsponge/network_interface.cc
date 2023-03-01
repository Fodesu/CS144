#include "network_interface.hh"

#include "address.hh"
#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "ethernet_header.hh"
#include "ipv4_datagram.hh"
#include "parser.hh"

#include <array>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <optional>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    
    if(_address_mp.find(next_hop_ip) != _address_mp.end()) {
        EthernetHeader header = {_address_mp[next_hop_ip].MacAddr , _ethernet_address, EthernetHeader::TYPE_IPv4};
        EthernetFrame frame;
        frame.header() = header;
        frame.payload() = dgram.serialize();
        _frames_out.push(frame);
    } else {
        printf("send arp \n");
        printf("%ul\n", next_hop_ip);
        if(_last_send_arp > 0) {
            printf("last send arp > 0 \n");
            _ip_queue[next_hop_ip].emplace_back(dgram);
            printf("end send \n");
            return;
        }

        EthernetFrame frame = Create_FrameToArp(next_hop_ip);
        _frames_out.push(frame);
        _last_send_arp = 5 * 1000;
        _ip_queue[next_hop_ip].emplace_back(dgram);
        _wait_arp_table.insert({next_hop_ip, 5 * 1000});
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    if(frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST) {
        return nullopt;
    }
    if(frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram dgram;
        if(dgram.parse(frame.payload()) != ParseResult::NoError)
            return nullopt;
        return dgram; 
    } else {
        ARPMessage message;
        if(message.parse(frame.payload()) != ParseResult::NoError) 
            return nullopt;
        if(message.target_ip_address != _ip_address.ipv4_numeric()) return nullopt; 
        EthernetAddress sender_eth_addr = message.sender_ethernet_address;
        uint32_t send_ip = message.sender_ip_address;
        
        if(message.opcode == ARPMessage::OPCODE_REQUEST) {
            _address_mp.insert({send_ip, {sender_eth_addr, 30 * 1000}});
            ARPMessage reply;
            reply.opcode = ARPMessage::OPCODE_REPLY;
            reply.target_ip_address = send_ip;
            reply.target_ethernet_address = sender_eth_addr;
            reply.sender_ip_address = _ip_address.ipv4_numeric();
            reply.sender_ethernet_address = _ethernet_address;

            EthernetFrame Replyframe;
            Replyframe.header() = {sender_eth_addr, _ethernet_address, EthernetHeader::TYPE_ARP};
            Replyframe.payload() = reply.serialize();
            _frames_out.push(Replyframe);
        } else {
            printf("in the _wait_arp of  reply \n");
            _wait_arp_table.erase(message.sender_ip_address);
            _address_mp[message.sender_ip_address].timecount = 30 * 1000;
            _address_mp[message.sender_ip_address].MacAddr = message.sender_ethernet_address;
            Address addr = Address::from_ipv4_numeric(message.sender_ip_address);
            if(_ip_queue.find(message.sender_ip_address)==_ip_queue.end()) printf("ip_queue is empty\n");
            else {
                std::deque<InternetDatagram> que = _ip_queue[message.sender_ip_address];
                _ip_queue.erase(message.sender_ip_address);
                while(!que.empty()) {
                    auto front = que.front();
                    send_datagram(front, addr);
                    printf("pop the ip_address\n");
                    que.pop_front();
                    printf("end of wait arp\n");
                }
            }
            printf("%ul\n", message.sender_ip_address);
        }
        return nullopt;
    }

}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    for(auto iter = _address_mp.begin(); iter != _address_mp.end();) {
        _last_send_arp -= min(_last_send_arp, ms_since_last_tick);
        iter->second.timecount -= min(iter->second.timecount, ms_since_last_tick);
        if(iter->second.timecount == 0) {
            printf("_address_mp\n");
            _address_mp.erase(iter++); //! 若不使用 iter++ 则使得迭代器失效
        } else {
            iter++;
        }
    }
    //! 如果ARP5秒内没有回复，就重新发送
    for(auto iter = _wait_arp_table.begin(); iter!= _wait_arp_table.end(); iter++) {
        iter->second -= min(iter->second, ms_since_last_tick);
        if(iter->second == 0) {
            EthernetFrame frame = Create_FrameToArp(iter->first);
            _frames_out.push(frame);
        }
    }

}

EthernetFrame NetworkInterface::Create_FrameToArp(uint32_t dst_ip) {
    EthernetHeader header = {ETHERNET_BROADCAST, _ethernet_address, EthernetHeader::TYPE_ARP};
    ARPMessage message;
    message.opcode = ARPMessage::OPCODE_REQUEST;
    message.sender_ethernet_address = _ethernet_address;
    message.target_ethernet_address = {0, 0, 0, 0};
    message.sender_ip_address = _ip_address.ipv4_numeric();
    message.target_ip_address = dst_ip;

    EthernetFrame frame;
    frame.header() = header;
    frame.payload() = message.serialize();
    return frame;
}