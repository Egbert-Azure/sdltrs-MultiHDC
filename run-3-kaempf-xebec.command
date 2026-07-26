#!/bin/bash
# Double-click in Finder: Kaempf CP/M 2.2 (Genie IIIs), standard EPROM, Xebec.
# This is the Kaempf disk that carries the Winchester tooling: CONFIG, WNFORMAT.
# Thin wrapper -- all the actual configuration lives in run-multihdc.command.
cd "$(dirname "$0")"
exec ./run-multihdc.command kaempf
