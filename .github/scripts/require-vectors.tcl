package require argparse
package require rbc::vector
package require tclsimrawreader
set fixture [file join [file dirname [info script]] .. .. test raw_files ngspice dc.raw]
set reader [::tclsimrawreader::openraw $fixture -output vector]
try {
    set vector [$reader vector v(out) -count 1]
    if {[$vector length] != 1} {error "Expected one sample from v(out)"}
    rbc::vector destroy $vector
} finally {
    $reader close
}
