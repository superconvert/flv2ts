#ifndef FLV2TS_FLV_PARSER_HH
#define FLV2TS_FLV_PARSER_HH

#include "header.hh"
#include "tag.hh"
#include "audio_tag.hh"
#include "video_tag.hh"
#include "script_data_tag.hh"
#include <aux/file_mapped_memory.hh>
#include <aux/byte_stream.hh>
#include <string>
#include <string.h>
#include <inttypes.h>

namespace flv2ts {
  namespace flv {
    class Parser {
    public:
      Parser(const char* filepath) 
        : _fmm(filepath),
          _in(_fmm.ptr<const uint8_t>(), _fmm.size())
      {
      }

      operator bool() const { return _fmm && _in; }

      bool parseHeader(Header& header) {
        if(! _in.can_read(9)) {
          return false;
        }
        
        header.signature[0] = _in.readUint8();
        header.signature[1] = _in.readUint8();
        header.signature[2] = _in.readUint8();
        if(strncmp("FLV", header.signature, 3) != 0) {
          return false;
        }
        
        header.version = _in.readUint8();
        
        uint8_t flags = _in.readUint8();
        
        header.is_audio = flags & 0x04;
        header.is_video = flags & 0x01;
        
        header.data_offset = _in.readUint32Be();

        return true;
      }

      bool parseTag(Tag& tag, uint32_t& prev_tag_size) {
        if(! _in.can_read(4)) {
          return false;
        }
        prev_tag_size = _in.readUint32Be();
        if(_in.eos()) {
          // last tag
          memset(&tag, 0, sizeof(tag)); 
          return true;
        } else {
          return parseTagImpl(tag);
        }
      }

      bool abs_seek(size_t pos) { return _in.abs_seek(pos); }
      bool rel_seek(ssize_t offset) { return _in.rel_seek(offset); }
      size_t position() const { return _in.position(); }
      bool eos() const { return _in.eos(); }
      
    private:
      bool parseTagImpl(Tag& tag) {
        if(! _in.can_read(11)) {
          return false;
        }
        
        uint8_t tmp = _in.readUint8();
        
        tag.filter = tmp & 0x20;
        if(tag.filter) {
          // encrypted file is unsupported
          return false;
        }

        tag.type = tmp & 0x1F;
        
        tag.data_size = _in.readUint24Be();
        
        uint32_t timestamp    = _in.readUint24Be();
        uint8_t timestamp_ext = _in.readUint8();
        tag.timestamp = static_cast<int32_t>((timestamp_ext << 24) + timestamp);
        
        tag.stream_id = _in.readUint24Be();
        
        if(! _in.can_read(tag.data_size)) {
          return false;
        }
        
        switch(tag.type) {
        case Tag::TYPE_AUDIO:       return parseAudioTag(tag);
        case Tag::TYPE_VIDEO:       return parseVideoTag(tag);
        case Tag::TYPE_SCRIPT_DATA: return parseScriptDataTag(tag);
        default:                    return false;  // undefined tag type
        }
      }

      bool parseAudioTag(Tag& tag) {
        _buf.resize(sizeof(AudioTag) + tag.data_size);
        
        AudioTag* audio = reinterpret_cast<AudioTag*>(const_cast<char*>(_buf.data())); // XXX:

        uint8_t tmp = _in.readUint8();
        audio->sound_format = (tmp & 0xF0) >> 4;
        audio->sound_rate   = (tmp & 0x0C) >> 2;
        audio->sound_size   = (tmp & 0x02) >> 1;
        audio->sound_type   = (tmp & 0x01);

        if(audio->sound_format == 10) { // AAC
          audio->aac_packet_type = _in.readUint8();
        }
        
        audio->payload = reinterpret_cast<uint8_t*>(audio) + audio->headerSize();
        _in.read(audio->payload, audio->payloadSize(tag));
        
        tag.data = static_cast<TagData*>(audio);
        return true;
      }

      bool parseVideoTag(Tag& tag) {
        _in.rel_seek(tag.data_size);
        
        return true;
      }

      bool parseScriptDataTag(Tag& tag) {
        _buf.resize(tag.data_size);

        ScriptDataTag* script_data = reinterpret_cast<ScriptDataTag*>(const_cast<char*>(_buf.data())); // XXX:
        _in.read(script_data->amf0_payload, tag.data_size);

        tag.data = static_cast<TagData*>(script_data);
        return true;
      }

    private:
      aux::FileMappedMemory _fmm;
      aux::ByteStream _in;
      std::string _buf;
    };
  }
}

#endif
