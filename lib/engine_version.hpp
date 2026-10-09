// OWNERSHIP=Claude
// Which eval a score was found with (#79). The PTT keeps every long search's
// result on disk across server restarts and engine rebuilds, so a stored score
// has to say what produced it: an entry from another EVAL_VERSION is shown as
// older and never seeds a search or plays a move.
//
// Bump EVAL_VERSION with any commit that changes what a search scores a
// position: eval() or basic_eval() terms, WEIGHTS_OG (the default weight set,
// weights/wN.txt, #85), the NNE format or how the net enters the eval, and
// search changes that move scores (pruning, extensions, mate or draw handling).
// Not for speedups that leave `make bench`'s scores alone, and not for a
// retrained net: entries carry the net's own hash, which the GUI compares as
// well. docs/WORKFLOW.md says the same.
#ifndef ENGINE_VERSION_HPP
#define ENGINE_VERSION_HPP

constexpr int EVAL_VERSION = 12;

// The commit the binary was built from, set by the Makefile ("-dirty" with
// uncommitted changes); only a provenance note, never compared.
#ifndef ENGINE_COMMIT
#define ENGINE_COMMIT "unknown"
#endif

#endif // ENGINE_VERSION_HPP
