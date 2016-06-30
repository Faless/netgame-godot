# About
The `netgame` module for godot allows for easy to use, painless and performant client/server application.

The module act as a network subsystem and is composed of 2 parts:

- `NetGameServer`: Act as a TCP and UDP server (requires a TCP and a UDP open port)
- `NetGameClient`: Act as a TCP and UDP client (do not require any open port)

Both parts manage the network packets behind the scenes and and exposes them through godot signals. Signals can be sent either in a separate thread or in process/fixed process.

# Installation
Being a module you will need to recompile godot from source. To do that:

1. Clone the godot engine repository
2. Copy the `netgame` folder from this repository into the godot `modules` folder
3. Recompile godot (see http://docs.godotengine.org/en/latest/reference/_compiling.html )

# Usage

For usage examples please refer to the project in the `demo` folder.

The methods should be self explainatory, the `rt` parameter when sending UDP packets will cause the receiving end to drop the packet if it is received out of order 

# Disclaimer

This module is in a very early development stage:

- The protocol is not yet very robust
- I'm planning to separate the TCP and UDP server in the future.
- The current client limit is 256
- The current commands limit is 255
- Heavy TCP usage will increase the UDP packet loss rate.

> THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
