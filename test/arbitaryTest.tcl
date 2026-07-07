package require tclsimrawreader
namespace import ::tclsimrawreader::*
set currentDir [file normalize [file dirname [info script]]]

set rbinary [tclsimrawreader::openraw [file join $currentDir raw_files ngspice ac_multiplot.raw]]
set rascii [tclsimrawreader::openraw [file join $currentDir raw_files ngspice ac_multiplot_ascii.raw]]

foreach r [list $rbinary $rascii] type {binary ascii} {
   # puts "$type: [$r plots]"
    puts "$type: [$r header -plot 0]"
    puts "$type: [$r names -plot 0]"
}
#puts [$rbinary vector i(vgain)]
#puts [$rascii vector i(vgain)]
