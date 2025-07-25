FB_IMPL_MSG(GSTAT, 1, gstat_unknown_switch, -901, "00", "000", "found unknown switch")
FB_IMPL_MSG(GSTAT, 2, gstat_retry, -901, "00", "000", "please retry, giving a database name")
FB_IMPL_MSG(GSTAT, 3, gstat_wrong_ods, -901, "00", "000", "Wrong ODS version, expected @1, encountered @2")
FB_IMPL_MSG(GSTAT, 4, gstat_unexpected_eof, -901, "00", "000", "Unexpected end of database file.")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 5, "gstat version @1")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 6, "\nDatabase \"@1\"")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 7, "\n\nDatabase file sequence:")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 8, "File @1 continues as file @2")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 9, "File @1 is the @2 file")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 10, "\nAnalyzing database pages ...")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 11, "    Primary pointer page: @1, Index root page: @2")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 12, "    Data pages: @1, data page slots: @2, average fill: @3")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 13, "    Fill distribution:")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 14, "    Index @1 (@2)")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 15, "	Depth: @1, leaf buckets: @2, nodes: @3")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 16, "	Average data length: @1, total dup: @2, max dup: @3")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 17, "	Fill distribution:")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 18, "    Expected data on page @1")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 19, "    Expected b-tree bucket on page @1 from @2")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 20, "unknown switch \"@1\"")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 21, "Available switches:")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 22, "    -a      analyze data and index pages")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 23, "    -d      analyze data pages")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 24, "    -h      analyze header page ONLY")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 25, "    -i      analyze index leaf pages")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 26, "    -l      analyze log page")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 27, "    -s      analyze system relations in addition to user tables")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 28, "    -z      display version number")
FB_IMPL_MSG(GSTAT, 29, gstat_open_err, -901, "00", "000", "Can't open database file @1")
FB_IMPL_MSG(GSTAT, 30, gstat_read_err, -901, "00", "000", "Can't read a database page")
FB_IMPL_MSG(GSTAT, 31, gstat_sysmemex, -901, "00", "000", "System memory exhausted")
FB_IMPL_MSG_SYMBOL(GSTAT, 32, gstat_username, "    -u      username")
FB_IMPL_MSG_SYMBOL(GSTAT, 33, gstat_password, "    -p      password")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 34, "    -r      analyze average record and version length")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 35, "    -t      tablename <tablename2...> (case sensitive)")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 36, "    -tr     use trusted authentication")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 37, "    -fetch  fetch password from file")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 38, "option -h is incompatible with options -a, -d, -i, -r, -schema, -s and -t")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 39, "usage:   gstat [options] <database> or gstat <database> [options]")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 40, "database name was already specified")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 41, "option -t needs a table name")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 42, "option -t got a too long table name @1")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 43, "option -t accepts several table names only if used after <database>")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 44, "table \"@1\" not found")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 45, "use gstat -? to get help")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 46, "    Primary pages: @1, secondary pages: @2, swept pages: @3")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 47, "    Big record pages: @1")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 48, "    Blobs: @1, total length: @2, blob pages: @3")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 49, "        Level 0: @1, Level 1: @2, Level 2: @3")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 50, "option -e is incompatible with options -a, -d, -h, -i, -r, -schema, -s and -t")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 51, "    -e      analyze database encryption")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 52, "Data pages: total @1, encrypted @2, non-crypted @3")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 53, "Index pages: total @1, encrypted @2, non-crypted @3")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 54, "Blob pages: total @1, encrypted @2, non-crypted @3")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 55, "no encrypted database support, only -e and -h can be used")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 56, "    Empty pages: @1, full pages: @2")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 57, "    -role   SQL role name")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 58, "Other pages: total @1, ENCRYPTED @2 (DB problem!), non-crypted @3")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 59, "Gstat execution time @1")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 60, "Gstat completion time @1")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 61, "    Expected page inventory page @1")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 62, "Generator pages: total @1, encrypted @2, non-crypted @3")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 63, "    Table size: @1 bytes")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 64, "        Level @1: @2, total length: @3, blob pages: @4")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 65, "    -sch    schemaname <schemaname2...> (case sensitive)")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 66, "option -sch needs a schema name")
FB_IMPL_MSG_NO_SYMBOL(GSTAT, 67, "option -sch got a too long schema name @1")
