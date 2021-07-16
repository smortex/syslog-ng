#!/usr/bin/env python
#############################################################################
# Copyright (c) 2021 Balabit
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################
import pytest

test_parameters_raw = [
    (r"""conn=1463 fd=12 ACCEPT from IP=[2607:5300:203:5234::]:50466 (IP=[::]:389)""", "<${.slapd.conn}><${.slapd.fd}><${.slapd.type}><${.slapd.clientip}><${.slapd.clientport}><${.slapd.serverip}><${.slapd.serverport}>", "<1463><12><ACCEPT><2607:5300:203:5234::><50466><::><389>"),
    (r"""conn=1463 op=0 EXT oid=1.3.6.1.4.1.1466.20037""", "<${.slapd.conn}><${.slapd.op}><${.slapd.type}><${.slapd.oid}>", "<1463><0><EXT><1.3.6.1.4.1.1466.20037>"),
    (r"""conn=1463 op=0 RESULT oid= err=0 text=""", "<${.slapd.conn}><${.slapd.op}><${.slapd.type}><${.slapd.oid}><${.slapd.err}><${.slapd.text}>", "<1463><0><RESULT><><0><>"),
    (r"""conn=1463 op=0 STARTTLS""", "<${.slapd.conn}><${.slapd.op}><${.slapd.type}>", "<1463><0><STARTTLS>"),
    (r'conn=1463 op=3 SRCH base="ou=people,dc=example,dc=com" scope=2 deref=0 filter="(&(uid=user)(objectClass=inetOrgPerson))"', "<${.slapd.conn}><${.slapd.op}><${.slapd.type}><${.slapd.base}><${.slapd.scope}><${.slapd.deref}><${.slapd.filter}>", """<1463><3><SRCH><ou=people,dc=example,dc=com><2><0><(&(uid=user)(objectClass=inetOrgPerson))>"""),
    (r"""conn=1463 op=2 SRCH attr=altServer namingContexts supportedCapabilities supportedControl supportedExtension supportedFeatures supportedLdapVersion supportedSASLMechanisms""", "<${.slapd.conn}><${.slapd.op}><${.slapd.type}><${.slapd.attr}>", """<1463><2><SRCH><altServer namingContexts supportedCapabilities supportedControl supportedExtension supportedFeatures supportedLdapVersion supportedSASLMechanisms>"""),
    (r"""conn=1459 op=4 SEARCH RESULT tag=101 err=0 nentries=0 text=""", "<${.slapd.conn}><${.slapd.op}><${.slapd.type}><${.slapd.tag}><${.slapd.err}><${.slapd.nentries}><${.slapd.text}>", """<1459><4><SEARCH RESULT><101><0><0><>"""),
]


@pytest.mark.parametrize(
    "input_message, template, expected_value", test_parameters_raw,
    ids=list(map(str, range(len(test_parameters_raw)))),
)
def test_slapd_parser(config, syslog_ng, input_message, template, expected_value):
    config.add_include("scl.conf")

    generator_source = config.create_example_msg_generator_source(num=1, template=config.stringify(input_message))
    checkpoint_parser = config.create_slapd_parser()

    file_destination = config.create_file_destination(file_name="output.log", template=config.stringify(template + "\n"))
    config.create_logpath(statements=[generator_source, checkpoint_parser, file_destination])

    syslog_ng.start(config)
    assert file_destination.read_log().strip() == expected_value
