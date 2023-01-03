load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

git_repository(
    name = "gtest",
    remote = "https://github.com/google/googletest",
    commit = "58d77fa8070e8cec2dc1ed015d66b454c8d78850",  # 1.12.x
    shallow_since = "1656350095 -0400",
)

git_repository(
    name = "fmt",
    commit = "a33701196adfad74917046096bf5a2aa0ab0bb50",  # 9.1.0
    shallow_since = "1661615830 -0700",
    remote = "https://github.com/fmtlib/fmt",
    patch_cmds = [
        "mv support/bazel/.bazelrc .bazelrc",
        "mv support/bazel/.bazelversion .bazelversion",
        "mv support/bazel/BUILD.bazel BUILD.bazel",
        "mv support/bazel/WORKSPACE.bazel WORKSPACE.bazel",
    ],
)
