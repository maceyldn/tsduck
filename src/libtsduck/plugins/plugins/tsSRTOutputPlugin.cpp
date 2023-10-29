//----------------------------------------------------------------------------
//
// TSDuck - The MPEG Transport Stream Toolkit
// Copyright (c) 2020-2023, Anthony Delannoy
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.
//
//----------------------------------------------------------------------------

#include "tsSRTOutputPlugin.h"
#include "tsPluginRepository.h"

#if !defined(TS_NO_SRT)
TS_REGISTER_OUTPUT_PLUGIN(u"srt", ts::SRTOutputPlugin);
#endif

//----------------------------------------------------------------------------
// Output constructor
//----------------------------------------------------------------------------

ts::SRTOutputPlugin::SRTOutputPlugin(TSP *tsp_) : AbstractDatagramOutputPlugin(tsp_, u"Send TS packets using Secure Reliable Transport (SRT)", u"[options] [address:port]", NONE),
                                                  _multiple(false),
                                                  _reconnect(true),
                                                  _restart_delay(100),
                                                  _sock()
{
    _sock.defineArgs(*this);

    option(u"multiple", 'm');
    help(u"multiple",
         u"When the receiver peer disconnects, wait for another one and continue.");

    option(u"reconnect", 'r');
    help(u"reconnect",
         u"When the receiver peer disconnects, wait for another one and continue to reconnect.");

    option(u"restart-delay", 0, UNSIGNED);
    help(u"restart-delay", u"milliseconds",
         u"With --multiple, wait the specified number of milliseconds before restarting.");

    // These options are legacy, now use --listener and/or --caller.
    option(u"", 0, STRING, 0, 1);
    help(u"", u"Local [address:]port. This is a legacy parameter, now use --listener.");

    option(u"rendezvous", 0, STRING);
    help(u"rendezvous", u"address:port", u"Remote address and port. This is a legacy option, now use --caller.");
}

//----------------------------------------------------------------------------
// Simple virtual methods.
//----------------------------------------------------------------------------

bool ts::SRTOutputPlugin::isRealTime()
{
    return true;
}

//----------------------------------------------------------------------------
// Output command line options method
//----------------------------------------------------------------------------

bool ts::SRTOutputPlugin::getOptions()
{
    _multiple = present(u"multiple");
    _reconnect = present(u"reconnect");
    getIntValue(_restart_delay, u"restart-delay", 0);

    return _sock.setAddresses(value(u""), value(u"rendezvous"), UString(), *tsp) &&
           _sock.loadArgs(duck, *this) &&
           AbstractDatagramOutputPlugin::getOptions();
}

//----------------------------------------------------------------------------
// Output start method
//----------------------------------------------------------------------------

bool ts::SRTOutputPlugin::start()
{
    // Call superclass first, then initialize SRT socket.
    tsp->verbose(u"Startup SRT Socket");
    return AbstractDatagramOutputPlugin::start() && _sock.open(*tsp);
}

//----------------------------------------------------------------------------
// Output stop method
//----------------------------------------------------------------------------

bool ts::SRTOutputPlugin::stop()
{
    // Call superclass first, then close SRT socket.
    if (AbstractDatagramOutputPlugin::stop())
    {
        //tsp->verbose(u"Parent Socket Closed");
        if (_sock.close(*tsp))
        {
            //tsp->verbose(u"Local Socket Closed");
            return true;
        }
    }
    return false;
}

//----------------------------------------------------------------------------
// Implementation of AbstractDatagramOutputPlugin: send one datagram.
//----------------------------------------------------------------------------
// Flag to indicate if stopping is in progress
bool isStopping = false;

bool ts::SRTOutputPlugin::sendDatagram(const void* address, size_t size)
{
// Loop on restart with multiple sessions.
for (;;)
{
    // Send the datagram.
    if (_sock.send(address, size, *tsp))
    {
        // Datagram sent successfully, exit the loop.
        return true;
    }

    //tsp->verbose(u"Sending datagram failed");

    // Check for actual error, not a clean disconnection from the receiver.
    //if (!_sock.peerDisconnected())
    //{
    //    tsp->verbose(u"Error occurred while sending datagram, do not retry");
    //    return false; // Error occurred, do not retry.
    //}

    tsp->verbose(u"Receiver disconnected%s", {_multiple ? u", waiting for another one" : u""});

    if (!_multiple)
    {
        tsp->verbose(u"No multiple sessions, terminate here");
        return false; // No multiple sessions, terminate here.
    }

 //   tsp->verbose(u"Stopping and restart");

    //stop();
    if (!stop())
    {
    //    tsp->verbose(u"Stop Failed");
        // Add additional error handling or logging if needed
    }

    //tsp->verbose(u"Sleeping");
    if (_restart_delay > 0)
    {
        SleepThread(_restart_delay);
    }

    //tsp->verbose(u"Starting");
    if (!start())
    {
        //tsp->verbose(u"Start Failed");
        // Add additional error handling or logging if needed
        if (!_reconnect)
        {
            tsp->verbose(u"Reconnect is disabled, terminate here");
            return false;
        }
    }
}

}
