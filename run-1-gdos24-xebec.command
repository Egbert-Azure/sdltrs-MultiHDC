#!/bin/bash
# Double-click in Finder: GDOS 2.4, standard EPROM, Xebec controller.
# Thin wrapper -- all the actual configuration lives in run-multihdc.command.
cd "$(dirname "$0")"
exec ./run-multihdc.command gdos
