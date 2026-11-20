def can_build(env, platform):
    return platform in ["windows", "linuxbsd", "macos"]


def configure(env):
    pass


def get_doc_classes():
    return [
        "GoetheDialogue",
        "GoetheDialogueRunner",
        "GoetheLocaleManager",
        "GoetheProject",
    ]


def get_doc_path():
    return "doc_classes"
