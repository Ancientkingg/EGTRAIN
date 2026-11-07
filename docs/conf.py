# Sphinx configuration of the documentation site. The table of contents is in index.md.

import re
from pathlib import Path

from docutils import nodes

project = "EGTRAIN"
extensions = ["myst_parser"]

myst_heading_anchors = 4
# tools/docs/check_docs.py checks every relative link, including the ones to files outside docs/ that
# Sphinx cannot resolve.
suppress_warnings = ["myst.xref_missing"]

html_theme = "furo"
html_title = "EGTRAIN documentation"

# The manual of the application, EGTRAIN.qch, builds with -t manual and the qthelp builder: the start page and
# docs/guides only. Its start page leaves out the developer paragraph and contents, and the landing page README is not part of it.
if tags.has("manual"):
    include_patterns = ["index.md", "guides/**"]
    suppress_warnings += ["toc.not_readable"]

# Pages link to the published C++ reference, so that the links also work in the manual, which holds no reference. In
# the site they point to the reference that the docs target builds next to the pages, which opens from disk.
API_URL = "https://egtrain.samuelbruin.com/latest/api/"

# Furo links its name at the foot of every page to the home of its code. The site links no code host, so the footer
# keeps the name without the link.
FURO_LINK = re.compile(r'<a href="[^"]+/furo">Furo</a>')

# Two licence notices in Furo's static files give an address on a code host, and so do Furo's source maps, which
# browsers load only for their developer tools. The site keeps the notices without the address and leaves out the
# source maps and the lines that name them.
CODE_HOST_ADDRESS = re.compile(rb"( \| |\n \* )(https?://)?github\.com/\S+")
SOURCE_MAP_LINE = re.compile(rb"\n/[*/]# sourceMappingURL=\S+")


def link_local_api(app, doctree, docname):
    for node in doctree.findall(nodes.reference):
        refuri = node.get("refuri", "")
        if refuri.startswith(API_URL):
            node["refuri"] = "../" * docname.count("/") + "api/" + refuri[len(API_URL) :]


def drop_furo_link(app):
    templates = getattr(app.builder, "templates", None)
    if templates:
        render = templates.render
        templates.render = lambda template, context: FURO_LINK.sub("Furo", render(template, context))


def drop_code_host_from_static(app, exception):
    static = Path(app.outdir, "_static")
    if exception is not None or app.builder.name != "html" or not static.is_dir():
        return
    for path in static.rglob("*"):
        if path.suffix == ".map":
            path.unlink()
        elif path.suffix in (".css", ".js", ".txt"):
            data = path.read_bytes()
            kept = SOURCE_MAP_LINE.sub(b"", CODE_HOST_ADDRESS.sub(b"", data))
            if kept != data:
                path.write_bytes(kept)


def drop_developer_contents(app, docname, source):
    if docname == "index":
        cut = source[0].find("The developer pages")
        if cut >= 0:
            source[0] = source[0][:cut]


def setup(app):
    app.connect("builder-inited", drop_furo_link)
    app.connect("build-finished", drop_code_host_from_static)
    # The manual holds no C++ reference, so its links keep the published address.
    if app.tags.has("manual"):
        app.connect("source-read", drop_developer_contents)
    else:
        app.connect("doctree-resolved", link_local_api)
