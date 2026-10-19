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

# Pages link to the published C++ reference, so that the links also work on GitHub. In the site they
# point to the reference that the docs target builds next to the pages, which opens from disk.
API_URL = "https://ancientkingg.github.io/EGTRAIN/latest/api/"


def link_local_api(app, doctree, docname):
    for node in doctree.findall(nodes.reference):
        if node.get("refuri", "").startswith(API_URL):
            node["refuri"] = "../" * docname.count("/") + "api/" + node["refuri"][len(API_URL) :]


def setup(app):
    app.connect("doctree-resolved", link_local_api)
