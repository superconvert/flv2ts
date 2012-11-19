#include <iostream>
#include <fstream>
#include <flv/parser.hh>
#include <adts/header.hh>
#include <ts/packet.hh>
#include <aux/crc32.hh>
#include <stdlib.h>
#include <string.h>

using namespace flv2ts;

const static unsigned PMT_PID = 0x1000;
const static unsigned ES_VIDEO_PID = 0x100;
const static unsigned ES_AUDIO_PID = 0x101;
const static unsigned STREAM_TYPE_VIDEO = 27;
const static unsigned STREAM_TYPE_AUDIO = 15;
const static unsigned PMT_TABLE_ID = 2;

void write_ts_pat(std::ostream& out) {
  char buf[256];
  ssize_t size;
  size_t wrote_size = 0;

  buf[0] = static_cast<char>(ts::Packet::SYNC_BYTE);
  wrote_size += 1;

  ts::Header h;
  h.transport_error_indicator = false;
  h.payload_unit_start_indicator = true;
  h.transport_priority = 0;
  h.pid = 0;
  h.scrambling_control = 0;
  h.adaptation_field_exist = 1; // payload unit only
  h.continuity_counter = 0;
  
  size = h.dump(buf + wrote_size, sizeof(buf) - wrote_size);
  wrote_size += size;
  
  ts::PAT pat;
  pat.pointer_field = 0;
  pat.table_id = 0;
  pat.section_syntax_indicator = true;
  pat.zero = 0;
  pat.reserved1 = 3;
  pat.transport_stream_id = 1;
  pat.version_number = 0;
  pat.reserved2 = 3;
  pat.current_next_indicator = 1;
  pat.section_number = 0;
  pat.last_section_number = 0;
  
  {
    ts::PMT_MAP_ENTRY entry;
    entry.program_num = 1;
    entry.reserved = 7;
    entry.program_pid = PMT_PID;

    pat.pmt_map.push_back(entry);
  }

  pat.section_length = 9 + pat.pmt_map.size()*4;
  
  {
    size = pat.dump(buf+wrote_size, sizeof(buf)-wrote_size);
    pat.crc32 = aux::chksum_crc32(buf + 5, wrote_size + size - 4 - 5);
  }
    
  size = pat.dump(buf+wrote_size, sizeof(buf)-wrote_size);
  wrote_size += size;

  for(; wrote_size < ts::Packet::SIZE; wrote_size++) {
    buf[wrote_size] = (char)0xFF;
  }

  out.write(buf, wrote_size);
}

void write_ts_pmt(std::ostream& out) {
  char buf[256];
  ssize_t size;
  size_t wrote_size = 0;

  buf[0] = static_cast<char>(ts::Packet::SYNC_BYTE);
  wrote_size += 1;

  ts::Header h;
  h.transport_error_indicator = false;
  h.payload_unit_start_indicator = true;
  h.transport_priority = 0;
  h.pid = PMT_PID;
  h.scrambling_control = 0;
  h.adaptation_field_exist = 1; // payload unit only
  h.continuity_counter = 0;
  
  size = h.dump(buf + wrote_size, sizeof(buf) - wrote_size);
  wrote_size += size;
  
  ts::PMT pmt;
  pmt.pointer_field = 0;
  pmt.table_id = PMT_TABLE_ID;
  pmt.section_syntax_indicator = true;
  pmt.zero = 0;
  pmt.reserved1 = 3;
  pmt.program_num = 1;
  pmt.version_number = 0;
  pmt.reserved2 = 3;
  pmt.current_next_indicator = 1;
  pmt.section_number = 0;
  pmt.last_section_number = 0;
  
  pmt.reserved3 = 7;
  pmt.pcr_pid = ES_VIDEO_PID; // PID of general timecode stream, or 0x1FFF
  pmt.reserved4 = 15;
  pmt.program_info_length = 0;

  {
    // video
    ts::STREAM_INFO info;
    info.stream_type    = STREAM_TYPE_VIDEO;
    info.reserved1      = 7;
    info.elementary_pid = ES_VIDEO_PID;
    info.reserved2      = 15;
    info.es_info_length = 0;
    pmt.stream_info_list.push_back(info);

    // audio
    info.stream_type    = STREAM_TYPE_AUDIO;
    info.elementary_pid = ES_AUDIO_PID;
    pmt.stream_info_list.push_back(info);
  }

  pmt.section_length = 13 + pmt.program_info_length + pmt.stream_info_list.size() * 5;
  for(size_t i=0; i < pmt.stream_info_list.size(); i++) {
    pmt.section_length += pmt.stream_info_list[i].es_info_length;
  }
  
  {
    size = pmt.dump(buf+wrote_size, sizeof(buf)-wrote_size);
    pmt.crc32 = aux::chksum_crc32(buf + 5, wrote_size + size - 4 - 5);
  }
    
  size = pmt.dump(buf+wrote_size, sizeof(buf)-wrote_size);
  wrote_size += size;

  for(; wrote_size < ts::Packet::SIZE; wrote_size++) {
    buf[wrote_size] = (char)0xFF;
  }

  out.write(buf, wrote_size);
}

void write_ts_start(std::ostream& out) {
  write_ts_pat(out);
  write_ts_pmt(out);
}

int main(int argc, char** argv) {
  if(argc != 4) {
    std::cerr << "Usage: flv2ts INPUT_FLV_FILE OUTPUT_DIR DURATION" << std::endl;
    return 1;
  }

  const char* flv_file = argv[1];
  const char* output_dir = argv[2];
  const unsigned duration = atoi(argv[3]);

  flv2ts::flv::Parser flv(flv_file);
  if(! flv) {
    std::cerr << "Can't open file: " << flv_file << std::endl;
    return 1;
  }
  
  std::cout << "[input]" << std::endl
            << "  flv:      " << flv_file << std::endl
            << "  output:   " << output_dir << std::endl
            << "  duration: " << duration << std::endl
            << std::endl;

  // flv header
  flv::Header flv_header;
  if(! flv.parseHeader(flv_header)) {
    std::cerr << "parse flv header failed" << std::endl;
    return 1;
  }
  flv.abs_seek(flv_header.data_offset);

  
  unsigned ts_seq=0;
  bool switched=true;
  std::ofstream ts_out;

  // flv body
  for(;;) {
    flv::Tag tag;
    uint32_t prev_tag_size;
    if(! flv.parseTag(tag, prev_tag_size)) {
      std::cerr << "parse flv tag failed" << std::endl;
      return 1;
    }
    
    if(flv.eos()) {
      break;
    }

    if(switched) {
      ts_out.close();

      char buf[1024];
      sprintf(buf, "%s/a-%d.ts", output_dir, ts_seq);
      ts_out.open(buf, std::ios::out | std::ios::binary);
      if(! ts_out) {
        std::cerr << "Can't open output file: " << buf << std::endl;
        return 1;
      }
      std::cout << "open: " << buf << std::endl;
      
      switched = false;
      ts_seq++;

      write_ts_start(ts_out);
    }

    switch(tag.type) {
    case flv::Tag::TYPE_AUDIO: {
      // audio
      if(tag.audio.sound_format != 10) { // 10=AAC
        std::cerr << "unsupported audio format: " << tag.audio.sound_format << std::endl;
        return 1;
      }
      if(tag.audio.aac_packet_type == 0) {
        // AudioSpecificConfig
        continue;
      }
      
      adts::Header adts = adts::Header::make_default(tag.audio.payload_size);
      char buf[7];
      adts.dump(buf, 7);
      //std::cout.write(buf, 7);
      //std::cout.write(reinterpret_cast<const char*>(tag.audio.payload), tag.audio.payload_size);
      break;
    }
      
    case flv::Tag::TYPE_VIDEO: {
      // video
      if(tag.video.codec_id != 7) { // 7=AVC
        std::cerr << "unsupported video codec: " << tag.video.codec_id << std::endl;
        return 1;
      }
      if(tag.video.avc_packet_type != 1) {
        // not AVC NALU
        continue;
      }
      
      //std::cout.write(reinterpret_cast<const char*>(tag.video.payload), tag.video.payload_size);        
      break;
    }

    default:
      break;
    }
  }
  std::cout << std::endl;
  return 0;
}
