#include "lob/itch_parser.hpp"
#include "lob/feed_handler.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

using namespace lob;

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --in <path>         Input ITCH 5.0 binary file\n"
              << "  --out <path>        Output LOB feed binary file\n"
              << "  --max <N>           Maximum messages to convert (default: all)\n"
              << "  --help              Display this message\n";
}

int main(int argc, char* argv[]) {
    std::string input_path;
    std::string output_path = "data/feed.bin";
    uint64_t max_msgs = std::numeric_limits<uint64_t>::max();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--in" && i + 1 < argc) {
            input_path = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--max" && i + 1 < argc) {
            max_msgs = std::stoull(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (input_path.empty()) {
        std::cerr << "Error: Input file required.\n";
        print_usage(argv[0]);
        return 1;
    }

    std::ifstream infile(input_path, std::ios::binary);
    if (!infile) {
        std::cerr << "Error: Could not open input file " << input_path << "\n";
        return 1;
    }

    BinaryFeedWriter writer;
    if (!writer.open(output_path)) {
        std::cerr << "Error: Could not open output file " << output_path << "\n";
        return 1;
    }

    std::cout << "Converting ITCH 5.0 from " << input_path << " to " << output_path << "...\n";

    uint64_t converted = 0;
    uint64_t skipped = 0;

    itch::Parser parser([&](const MarketEvent& ev) {
        writer.write_event(ev);
        ++converted;
    });

    // In NASDAQ ITCH, messages don't have a standardized length prefix natively unless wrapped in SoupBinTCP.
    // However, the standard historical data files format usually has a 2-byte message length prefix or relies on known message types.
    // If we assume a SoupBinTCP / historical file format where each message has a known size based on type, we can read the type.
    // For this demonstration, we'll read the message type, lookup the length, and read the payload.

    // A mapping of ITCH 5.0 Message lengths
    uint8_t type = 0;
    while (infile.read(reinterpret_cast<char*>(&type), 1) && converted + skipped < max_msgs) {
        // Read the rest of the message based on the type
        size_t len = 0;
        switch (type) {
            case 'A': len = sizeof(itch::AddOrder); break;
            case 'F': len = sizeof(itch::AddOrderMPID); break;
            case 'E': len = sizeof(itch::OrderExecuted); break;
            case 'C': len = sizeof(itch::OrderExecutedWithPrice); break;
            case 'X': len = sizeof(itch::OrderCancel); break;
            case 'D': len = sizeof(itch::OrderDelete); break;
            case 'U': len = sizeof(itch::OrderReplace); break;
            // The following are not handled by our struct currently, using standard ITCH 5.0 lengths
            case 'S': len = 12; break; // System Event
            case 'R': len = 39; break; // Stock Directory
            case 'H': len = 25; break; // Stock Trading Action
            case 'Y': len = 20; break; // Reg SHO
            case 'L': len = 26; break; // Market Participant Position
            case 'V': len = 35; break; // MWCB Decline Level
            case 'W': len = 12; break; // MWCB Status
            case 'K': len = 28; break; // IPO Quoting Period Update
            case 'J': len = 35; break; // LULD Auction Collar
            case 'h': len = 21; break; // Operational Halt
            case 'P': len = 44; break; // Trade
            case 'Q': len = 40; break; // Cross Trade
            case 'B': len = 19; break; // Broken Trade
            case 'I': len = 50; break; // NOII
            case 'N': len = 20; break; // Retail Interest
            default:
                std::cerr << "Unknown message type: " << type << " (char: " << (char)type << ")\n";
                return 1;
        }

        std::vector<uint8_t> buffer(len + 1); // +1 because we already read the type? 
        // Wait, the lengths above usually *include* the type byte if reading the whole message!
        // But we already read the type byte. So we need to read len-1 bytes.
        buffer[0] = type;
        if (!infile.read(reinterpret_cast<char*>(buffer.data() + 1), len - 1)) {
            break;
        }

        if (!parser.parse_message(buffer.data(), len)) {
            ++skipped;
        }
    }

    writer.close();
    std::cout << "Done. Converted: " << converted << ", Skipped (unhandled): " << skipped << "\n";

    return 0;
}
