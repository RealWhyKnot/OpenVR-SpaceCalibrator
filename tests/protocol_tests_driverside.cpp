#include <openvr_driver.h>

#include <cstdio>
#include <string>

#include "Protocol.h"
#include "protocol_layout_fill.h"

WireLayout DriverWireLayout()
{
	return FillWireLayout();
}
