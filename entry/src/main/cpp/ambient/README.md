# Ambient sound engine (prior code from a teammate)

Source: https://github.com/Xp4blos/hack-yeah-2026 (`engine/`), commit `512e41d` ("Polishing the reliability"),
written by a member of the Safe'n'Sound team for this hackathon (with AI assistance, see that repository's
`AI_WORKFLOW.md`). It is the team's own work and, like this repository, has deliberately no license file.

Included unchanged: `include/`, `src/` (`fft`, `detector`, `custom`, `trainer`), `tests/` (`test_core`,
`test_custom`, `test_safety`, `tests/data`), `tools/wav_cli.cpp`, `tools/gen_test_wavs.py`, `CHANGES.md`.
Left out: `speech.*` and `speech_test.cpp` (keyword detection is roadmap only).

Used by the app through `../wrapper/sound_engine.*`.
