#pragma once
#include <streambuf>

class buffersource : public std::basic_streambuf<char>
{
public:
	buffersource(std::vector<uint8_t>&& buffer)
		: buffer(buffer) 
	{
		char* p = (char*)this->buffer.data();
		size_t n = this->buffer.size();
		setg(p, p, p + n);
		setp(p, p + n);
	}

	std::vector<uint8_t> buffer;
};
