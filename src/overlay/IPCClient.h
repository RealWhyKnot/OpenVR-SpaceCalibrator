#pragma once

#include "HandshakeOutcome.h"
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

	spacecal::HandshakeOutcome LastHandshakeOutcome() const { return lastOutcome; }

	protocol::Response SendBlocking(const protocol::Request& request);

	void Send(const protocol::Request& request);
	protocol::Response Receive();

private:
	protocol::Response ReceiveRaw(DWORD& bytesRead, bool& moreData);

	HANDLE pipe = INVALID_HANDLE_VALUE;
	spacecal::HandshakeOutcome lastOutcome = spacecal::HandshakeOutcome::Unavailable;
};
