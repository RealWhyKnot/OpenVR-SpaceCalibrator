#pragma once

#include "Protocol.h"

#include <string>

class IPCClient
{
public:
	~IPCClient();

	void Connect();
	bool TryConnect(std::string& error);
	void Disconnect();
	bool IsConnected() const { return pipe != nullptr && pipe != INVALID_HANDLE_VALUE; }

	protocol::Response SendBlocking(const protocol::Request& request);

	void Send(const protocol::Request& request);
	protocol::Response Receive();

private:
	HANDLE pipe = INVALID_HANDLE_VALUE;
};
