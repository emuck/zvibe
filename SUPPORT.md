# Getting Help

This document explains how to get support for ZVibe.

## Documentation

Start with the documentation in the `docs/` directory:

**Getting Started:**
- [README.md](README.md) - Project overview and quick start
- [QUICKSTART.md](docs/QUICKSTART.md) - 5-minute build guide
- [INSTALL.md](docs/INSTALL.md) - Dependency installation for all platforms
- [USER_GUIDE.md](docs/USER_GUIDE.md) - Command-line usage

**Troubleshooting:**
- [TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) - Common problems and solutions
- [TESTING.md](docs/TESTING.md) - Validation and testing procedures

**Platform-Specific:**
- [TARGETS.md](docs/TARGETS.md) - Target platform specifications
- [FPGA.md](docs/FPGA.md) - RISC-V FPGA implementation details
- Target README files in `target/*/README.md`

## Search Existing Issues

Before asking for help:

1. Search [existing issues](https://github.com/emuck/zvibe/issues)
2. Check [closed issues](https://github.com/emuck/zvibe/issues?q=is%3Aissue+is%3Aclosed)
3. Review [discussions](https://github.com/emuck/zvibe/discussions) (if enabled)

Many common questions have been answered.

## Reporting Bugs

For bugs, use the [bug report template](https://github.com/emuck/zvibe/issues/new?template=bug_report.md).

**Include:**
- Operating system and version
- Target platform (console, windows, same51, riscv)
- ZVibe version or commit hash
- Complete build and runtime logs
- Steps to reproduce

See CONTRIBUTING.md for details.

## Asking Questions

### GitHub Discussions

Use [GitHub Discussions](https://github.com/emuck/zvibe/discussions) for:
- General questions about using ZVibe
- Platform-specific setup questions
- Architecture or design questions
- Story file compatibility questions

### GitHub Issues

Use [GitHub Issues](https://github.com/emuck/zvibe/issues) only for:
- Bug reports (use template)
- Feature requests (use template)
- Documentation errors

Do not use issues for general questions (use Discussions instead).

## Response Time

**Expected response times:**
- Critical bugs: 48 hours
- General bugs: 7 days
- Feature requests: 14 days
- Questions: Best effort (community-driven)

**Note:** ZVibe is maintained by volunteers. Response times are targets, not guarantees.

## Community Guidelines

Follow the [Code of Conduct](CODE_OF_CONDUCT.md) in all interactions.

**Be respectful:**
- Keep discussions professional and technical
- Provide context and details
- Show appreciation for help received

**Be helpful:**
- Search before asking
- Provide complete information
- Share solutions you find
- Help others when you can

## Security Issues

**Do not report security vulnerabilities through public issues or discussions.**

See [SECURITY.md](SECURITY.md) for the security reporting process.

## Contributing

Interested in contributing code or documentation?

See [CONTRIBUTING.md](CONTRIBUTING.md) for:
- Development setup
- Code style guidelines
- Testing procedures
- Pull request process

## Professional Support

For commercial support, consulting, or custom development:

**Contact:** [maintainer email address]

**Services available:**
- Custom target ports
- Integration consulting
- Training and documentation
- Priority bug fixes
- Feature development

## Contact Information

**Maintainer:** Martin R. Raumann

**GitHub:** https://github.com/emuck/zvibe

**Issues:** https://github.com/emuck/zvibe/issues

**Discussions:** https://github.com/emuck/zvibe/discussions

## Additional Resources

**Z-machine Resources:**
- [Z-Machine Standards Document](https://www.inform-fiction.org/zmachine/standards/)
- [IF Archive](https://www.ifarchive.org/) - Story file repository

**Third-Party Components:**
- [SERV CPU](https://github.com/olofk/serv) - RISC-V CPU core
- [Vivado Documentation](https://www.xilinx.com/support/documentation-navigation.html) - FPGA tools

**Development Tools:**
- [Doxygen Manual](https://www.doxygen.nl/manual/) - API documentation generator
- [Verilator Manual](https://verilator.org/guide/latest/) - Verilog simulator
