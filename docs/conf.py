# Sphinx configuration of the documentation site. The table of contents is in index.md.

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

# Pages link to the published C++ reference, so that the links also work on GitHub. In the site they
# point to the reference that the docs target builds next to the pages, which opens from disk.
API_URL = "https://ancientkingg.github.io/EGTRAIN/latest/api/"


def link_local_api(app, doctree, docname):
    for node in doctree.findall(nodes.reference):
        if node.get("refuri", "").startswith(API_URL):
            node["refuri"] = "../" * docname.count("/") + "api/" + node["refuri"][len(API_URL) :]


def drop_developer_contents(app, docname, source):
    if docname == "index":
        cut = source[0].find("The developer pages")
        if cut >= 0:
            source[0] = source[0][:cut]


def setup(app):
    # The manual holds no C++ reference, so its links keep the published address.
    if app.tags.has("manual"):
        app.connect("source-read", drop_developer_contents)
    else:
        app.connect("doctree-resolved", link_local_api)
