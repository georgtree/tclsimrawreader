package require ruff
package require tclsimrawreader
package require fileutil

set docDir [file dirname [file normalize [info script]]]
set sourceDir [file join $docDir ..]
source [file join $docDir startPage.ruff]
source [file join $docDir rbcBindings.ruff]
source [file join $docDir notesAndInternals.ruff]

set packageVersion [package provide tclsimrawreader]
if {$packageVersion eq {}} {
    return -code error "Package version is empty"
}
set title "Tcl tclsimrawreader package"

set commonSphinx [list -title $title -sortnamespaces false -preamble $startPage -pagesplit namespace -recurse false\
                    -includesource false -pagesplit namespace -autopunctuate true -compact false -includeprivate false -excludeprocs {^(Native.*|Dispatch)$}\
                    -product tclsimrawreader -diagrammer "ditaa --border-width 1" -version $packageVersion\
                    -copyright "George Yashin" {*}$::argv]
set commonNroff [list -title $title -sortnamespaces false -preamble $startPage -pagesplit namespace -recurse false\
                         -pagesplit namespace -autopunctuate true -compact false -includeprivate false -excludeprocs {^(Native.*|Dispatch)$}\
                         -product tclsimrawreader -diagrammer "ditaa --border-width 1" -version $packageVersion\
                         -copyright "George Yashin" {*}$::argv]

set namespaces [list rbcBindings ::tclsimrawreader {::Notes and internals}]

ruff::document $namespaces -format sphinx -outfile tclsimrawreader.rst -outdir [file join $docDir sphinx]\
        {*}$commonSphinx
ruff::document $namespaces -format nroff -outdir $docDir -outfile tclsimrawreader.n {*}$commonNroff

::fileutil::appendToFile [file join $docDir sphinx conf.py] {
extensions = list(dict.fromkeys(globals().get("extensions", []) + [
    "sphinx_rtd_theme",
    "sphinx.ext.githubpages",
]))
suppress_warnings = [
    "image.not_readable",
]
html_theme = "sphinx_rtd_theme"
html_theme_options = {
    "collapse_navigation": False,
    "sticky_navigation": True,
    "navigation_depth": 4,
    "includehidden": True,
    "titles_only": False,
}
# Custom CSS is staged by doc_gen.tcl; the source lives inside the HTML outdir.
html_static_path = []
html_css_files = list(globals().get("html_css_files", [])) + ["manual.css"]
from pygments.lexers.tcl import TclLexer
from pygments.token import Operator

class MyTclLexer(TclLexer):
    def get_tokens_unprocessed(self, text):
        for i, t, v in super().get_tokens_unprocessed(text):
            if v == "=":
                yield i, Operator, v   # or Name.Builtin
            elif v == "$":
                yield i, Operator, v   # or Name.Builtin
            elif v == "\\":
                yield i, Operator, v   # or Name.Builtin
            elif v == "%":
                yield i, Operator, v   # or Name.Builtin
            elif v == "'":
                yield i, Operator, v   # or Name.Builtin
            else:
                yield i, t, v

def setup(app):
    from sphinx.highlighting import lexers
    lexers["tcl"] = MyTclLexer()
}

# Use one Python environment for Sphinx and the RTD theme; propagate failures.
set python python
if {[info exists ::env(DOC_PYTHON)] && $::env(DOC_PYTHON) ne {}} {
    set python $::env(DOC_PYTHON)
}
file mkdir [file join $docDir _static]
file copy -force [file join $docDir theme manual.css] [file join $docDir _static manual.css]
exec -- $python -m sphinx -E -a -b html [file join $docDir sphinx] $docDir >@stdout 2>@stderr

# nroff pages names processing
foreach file [glob -directory $docDir *.n] {
    set old $file
    set tmp [file join $docDir __temp_rename__.n]
    set new [file join $docDir [string tolower [file tail $file]]]
    if {$old eq $new} {continue}
    file rename -force $old $tmp
    file rename -force $tmp $new
}
set specialPages [list tclsimrawreader-notes-and-internals]
foreach namespacePath $namespaces {
    set tails [list]
    while {$namespacePath ne {}} {
        set tail [string tolower [namespace tail $namespacePath]]
        regsub -all {\s+} [string trim $tail] {-} tail
        set namespacePath [namespace qualifiers $namespacePath]
        lappend tails $tail
    }
    lappend tails [string tolower tclsimrawreader]
    set manFileName [join [lreverse $tails] -]
    if {$manFileName ni $specialPages} {
        lappend manFilesLinks "${manFileName}(n)"
    }
}

set linksString ".SH SEE ALSO
tclsimrawreader(n) - package's main page
.br
tclsimrawreader-notes-and-internals(n) - information about implementation
.br
.sp 1
Public commands documentation:
.br
[join $manFilesLinks \n.br\n]"

proc addLinks2man {fileContents} {
    global linksString
    append fileContents "\n$linksString"
    return $fileContents
}

foreach file [glob -directory $docDir *.n] {
    fileutil::updateInPlace $file addLinks2man
}
