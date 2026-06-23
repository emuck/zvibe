## Description

Provide a clear description of the changes in this pull request.

## Type of Change

Select the type of change:

- [ ] Bug fix (non-breaking change that fixes an issue)
- [ ] New feature (non-breaking change that adds functionality)
- [ ] Breaking change (fix or feature that changes existing behavior)
- [ ] Documentation update
- [ ] Code refactoring (no functional changes)
- [ ] Build system update
- [ ] Test additions or improvements

## Related Issues

Closes: #[issue number]
Fixes: #[issue number]
See also: #[related issue]

## Testing Performed

Describe the testing performed to verify these changes.

**Targets tested:**
- [ ] Console (Linux/macOS/Windows)
- [ ] Windows (cross-compiled)
- [ ] SAM E51
- [ ] RISC-V FPGA
- [ ] Simulation (if applicable)

**Test commands:**
```bash
# Commands used to test
make test
make validate
```

**Test results:**
```
[Paste relevant test output or validation results]
```

## Validation Checklist

- [ ] Ran `make validate` successfully (console + unit tests; `make sim-tier1` if Verilator is available)
- [ ] Console tests pass: `cd target/console/tests && make test`
- [ ] Code follows style guide (DEVELOPMENT.md)
- [ ] Commit messages follow format: `component: description`
- [ ] No debugging code, commented-out blocks, or TODO comments added
- [ ] Documentation updated (if public API changed)
- [ ] CHANGELOG.md updated (if user-visible change)

## Code Quality

- [ ] Added Doxygen headers for new public functions
- [ ] Added unit tests for new functionality (if applicable)
- [ ] Checked bounds on all array accesses
- [ ] Validated all untrusted input
- [ ] No compiler warnings introduced
- [ ] Code builds on all targets (or gracefully skips)

## Documentation

**Documentation changes:**
- [ ] Updated README.md (if needed)
- [ ] Updated relevant docs/ files
- [ ] Added inline comments for complex logic
- [ ] Documentation style: technical, matter-of-fact tone; no emojis, hype words, or casual language

**Files modified:**
- [List of documentation files changed]

## Memory Impact

**RAM usage change:**
- Increase: [bytes or "none"]
- Decrease: [bytes or "none"]
- Impact on embedded targets: [description]

**Flash/code size change:**
- Increase: [bytes or "none"]
- Decrease: [bytes or "none"]

## Breaking Changes

If this PR includes breaking changes, describe:

**What breaks:**
- [API changes, behavior changes, etc.]

**Migration path:**
- [How users should adapt to the changes]

**Deprecation:**
- [What is being deprecated or removed]

## Screenshots or Logs

If applicable, add screenshots, waveforms, or log outputs:

```
[Paste relevant output here]
```

## Additional Notes

Add any additional context, concerns, or open questions:

- [Implementation decisions]
- [Known limitations]
- [Future work needed]
- [Questions for reviewers]

## Reviewer Notes

**Review focus areas:**
- [Specific areas that need careful review]
- [Concerns or trade-offs]
- [Alternative approaches considered]

## Checklist

- [ ] Self-reviewed code before submitting
- [ ] All tests pass
- [ ] Documentation complete
- [ ] No breaking changes (or properly documented)
- [ ] Ready for merge
