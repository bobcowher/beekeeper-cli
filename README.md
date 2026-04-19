# Beekeeper CLI

A native C++ command-line interface for managing AI training runs on a Beekeeper server. Designed to provide a "Synthesized View" of complex metrics for both human operators and AI agents.

## Features

- **Synthesized Analysis:** `beekeeper run analyze` aggregates TensorBoard trends and log metrics into a single high-signal verdict.
- **Project Lifecycle:** List, start, stop, and check status of projects.
- **Log Streaming:** Fetch the latest log output directly to your terminal.
- **Portability:** Compiled into a single native binary with zero runtime dependencies.

## Installation

### Dependencies (Build only)
- `g++` (C++17 support)
- `make`

### Building
```bash
git clone https://github.com/robertcowher/beekeeper-cli
cd beekeeper-cli
make
```

### Configuration
Set the following environment variables:
```bash
export BEEKEEPER_HOST="http://your-server:5000"
export BEEKEEPER_API_KEY="your-api-key"
```

## Usage

```bash
# List all projects
./beekeeper projects list

# Get a "Synthesized View" of a run
./beekeeper run analyze breakout-world-models

# Start training
./beekeeper training start breakout-world-models

# Get last 50 log lines
./beekeeper logs get breakout-world-models 50
```

## Architecture
The CLI is built as a thin, intelligent wrapper over the Beekeeper REST API. It uses single-header libraries for zero-dependency portability:
- `nlohmann/json` for JSON parsing.
- `yhirose/cpp-httplib` for HTTP communication.
