# Package size and Linux Pango recovery

Work only in feature/windows-workspace. Keep functional, security, signature, dependency closure and size checks active; retain current budgets.

1. Reproduce exact Pango SONAME failure with positive and negative distribution-policy tests, then classify the OS library and run the Python suite.
2. Measure Windows executable sections and compiler experiments in isolated build directories; adopt only evidence-backed reductions without feature removal.
3. Measure macOS executable sections and copied symbol-stripping variants on both architectures through CI, before changing signing inputs.
4. Apply justified packaging/compiler improvements, run targeted and full functional checks, compare package sizes with the unchanged baseline, and inspect all CI jobs.
5. Record measured results and any remaining blockers accurately.
