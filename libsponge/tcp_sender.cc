#include "tcp_sender.hh"

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "wrapping_integers.hh"

#include <cstdio>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _stream(capacity) 
    , _has_syn(false)
    , _has_fin(false)
    , _receiveWindowsSize(1) //! see lab3 document !!!
    , _outstandingMap()
    , _Timer(retx_timeout, 0)
    , _bytes_in_flight(0)
    , _consecutive_retransmissions((0)) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    size_t curr_windows_size = _receiveWindowsSize ? _receiveWindowsSize : 1;

    while(curr_windows_size > _bytes_in_flight) {
        TCPSegment segment;
        if(!_has_syn) {
            segment.header().syn = true;
            _has_syn = true;
        }
        segment.header().seqno = next_seqno();
        uint64_t data_size = std::min(TCPConfig::MAX_PAYLOAD_SIZE,
            curr_windows_size -  _bytes_in_flight - segment.header().syn);

        string payload = _stream.read(data_size);



        if(!_has_fin && _stream.eof() && curr_windows_size > (_bytes_in_flight + payload.size())) {
            _has_fin = true;
            segment.header().fin = true;
        }

        segment.payload() = Buffer(std::move(payload));

        if(segment.length_in_sequence_space() == 0) 
            break;

        if(_outstandingMap.empty()) {
            _Timer.SetRtoDefult();
            _Timer.CountZero();
        }

        _segments_out.push(segment);

        _bytes_in_flight += segment.length_in_sequence_space();
        _outstandingMap.insert({_next_seqno, segment});

        _next_seqno += segment.length_in_sequence_space();

        if(segment.header().fin)
            break;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ackno = unwrap(ackno, _isn, _next_seqno);
    
    if(abs_ackno > _next_seqno) {
        return;
    }

    for(auto iter = _outstandingMap.begin(); iter!= _outstandingMap.end(); ) {
        TCPSegment front = iter->second;
        if(abs_ackno >= iter->first + front.length_in_sequence_space()) {
            _bytes_in_flight -= front.length_in_sequence_space();
            iter = _outstandingMap.erase(iter);
            _Timer.SetRtoDefult();
            _Timer.CountZero();
            
        }
        else {
            break;
        }
    }
    _consecutive_retransmissions = 0;
    _receiveWindowsSize = window_size;
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    _Timer.CountIncrease(ms_since_last_tick);

    auto iter = _outstandingMap.begin();
    if (iter != _outstandingMap.end() && _Timer.CountOver()) {
        if (_receiveWindowsSize > 0)
            _Timer.SetRtoDouble();
        _Timer.CountZero();
        _segments_out.push(iter->second);
        ++_consecutive_retransmissions;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    segment.header().seqno = next_seqno();
    _segments_out.push(segment);
}


void TCPSender::Timer::SetRtoDefult() { RTO = _initial_retransmission_timeout;}

void TCPSender::Timer::SetRtoDouble() { RTO *= 2;}

bool TCPSender::Timer::CountOver() const {return count >= RTO; }

void TCPSender::Timer::CountIncrease(const size_t t) { count += t;}

void TCPSender::Timer::CountZero() { count = 0; }