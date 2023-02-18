#include "stream_reassembler.hh"
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <vector>
// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
:_end_flag(false), _eof_index(0), _excepted_index(0), 
 mp(), _output(capacity), _capacity(capacity)  {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    if (eof) {
        _eof_index = data.size() + index;
        _end_flag = true;
    }
    // not expect segement, cache it
    if (index > _excepted_index) {
        merge_str(data, index);
        return;
    } 

    // expect segment, write it to ByteStream
    int start_pos = _excepted_index - index;
    int write_cnt = data.size() - start_pos;
    // not enough space
    if (write_cnt < 0) {
        return;
    }
    _excepted_index += _output.write(data.substr(start_pos, write_cnt));

    // search the next segment
    std::vector<size_t> pop_list;
    for (auto segment : mp) {
        // already process or empty string
        if (segment.first + segment.second.size() <= _excepted_index || segment.second.size() == 0) {
            pop_list.push_back(segment.first);
            continue;
        } 

        // not yet
        if (_excepted_index < segment.first) {
            continue;
        }

        start_pos = _excepted_index - segment.first;
        write_cnt = segment.second.size() - start_pos;
        _excepted_index += _output.write(segment.second.substr(start_pos, write_cnt));
        pop_list.push_back(segment.first);
    }
    // remove the useless segment
    for (auto segment_id : pop_list) {
        mp.erase(segment_id);
    }

    if (_excepted_index == _eof_index && _end_flag) {
        _output.end_input();
    }
}

void StreamReassembler::merge_str(const std::string &data, size_t index) {
    size_t data_left = index;
    size_t data_right = index + data.size();
    std::string data_copy = data;
    std::vector<int> remove_list;
    bool is_merge = true;
    
    for(auto segment : mp) {
        size_t segment_left = segment.first;
        size_t segment_right = segment.first + segment.second.size();

        if(data_left <= segment_left && data_right >= segment_left) {
            if(data_right >= segment_right) { // >= or >
                remove_list.push_back(segment.first);
                continue;
            }

            if(data_right < segment_right) {
                data_copy = data_copy.substr(0, segment_left - data_left) + segment.second;
                data_right = data_left + data_copy.size();
                remove_list.push_back(segment.first);
            }
        }

        if(data_left > segment_left && data_left < segment_right) {
            if(data_right <= segment_right) {
                is_merge = false;
            }

            if(data_right > segment_right) {
                data_copy = segment.second.substr(0, data_left - segment_left) + data_copy;
                data_left = segment_left;
                data_right = data_left + data_copy.size();
                remove_list.push_back(segment.first);
            }
        }
    }

    for(auto &c : remove_list) {
        mp.erase(c);
    }

    if(is_merge)
        mp[data_left] = data_copy;
}

size_t StreamReassembler::unassembled_bytes() const { 
    size_t cnt = 0;
    for(auto &c: mp) {
        cnt += c.second.size();
    }
    return cnt;
}

bool StreamReassembler::empty() const { return unassembled_bytes() == 0; }
