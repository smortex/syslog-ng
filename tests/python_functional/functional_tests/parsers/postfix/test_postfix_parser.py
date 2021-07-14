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
    (r"""7C752BB885: client=localhost[127.0.0.1]""", "<${.postfix.message_id}><${.postfix.client.hostname}><${.postfix.client.ip}>", "<7C752BB885><localhost><127.0.0.1>"),
    (r"""7C752BB885: message-id=<217472618235967.1626217378.087954998016357-foo-1294-sale.order@example>""", "<${.postfix.message_id}><${.postfix.message-id}>", "<7C752BB885><217472618235967.1626217378.087954998016357-foo-1294-sale.order@example>"),
    (r"""7C752BB885: from=<bounce+568-sale.order-1294@example.com>, size=117746, nrcpt=1 (queue active)""", "<${.postfix.message_id}><${.postfix.from}><${.postfix.size}><${.postfix.nrcpt}><${MESSAGE}>", "<7C752BB885><bounce+568-sale.order-1294@example.com><117746><1><queue active>"),
    (r"""7C752BB885: to=<recipient@example.com>, relay=example.com[127.0.0.1]:25, delay=2.1, delays=0.27/0.02/0.91/0.92, dsn=2.0.0, status=sent (250 2.0.0 OK  1626217380 e5pl239890psj.21 - esmtp)""", "<${.postfix.message_id}><${.postfix.to}><${.postfix.relay.hostname}><${.postfix.relay.ip}><${.postfix.relay.port}><${.postfix.delay}><${.postfix.delays.pdelay}><${.postfix.delays.adelay}><${.postfix.delays.sdelay}><${.postfix.delays.xdelay}><${.postfix.dsn}><${.postfix.status}><${MESSAGE}>", "<7C752BB885><recipient@example.com><example.com><127.0.0.1><25><2.1><0.27><0.02><0.91><0.92><2.0.0><sent><250 2.0.0 OK  1626217380 e5pl239890psj.21 - esmtp>"),
    (r"""7C752BB885: removed""", "<${.postfix.message_id}><${MESSAGE}>", "<7C752BB885><removed>"),

    (r"""1E378BB928: to=<bounce+569-sale.order-1295@vps249428.vittoria.pro>, relay=local, delay=0.02, delays=0/0.01/0/0.01, dsn=5.1.1, status=bounced (unknown user: "bounce")""", "<${.postfix.message_id}><${.postfix.to}><${.postfix.relay}><${.postfix.delay}><${.postfix.delays.pdelay}><${.postfix.delays.adelay}><${.postfix.delays.sdelay}><${.postfix.delays.xdelay}><${.postfix.dsn}><${.postfix.status}><${MESSAGE}>", r"""<1E378BB928><bounce+569-sale.order-1295@vps249428.vittoria.pro><local><0.02><0><0.01><0><0.01><5.1.1><bounced><unknown user: "bounce">"""),
]


@pytest.mark.parametrize(
    "input_message, template, expected_value", test_parameters_raw,
    ids=list(map(str, range(len(test_parameters_raw)))),
)
def test_postfix_parser(config, syslog_ng, input_message, template, expected_value):
    config.add_include("scl.conf")

    generator_source = config.create_example_msg_generator_source(num=1, template=config.stringify(input_message))
    checkpoint_parser = config.create_postfix_parser()

    file_destination = config.create_file_destination(file_name="output.log", template=config.stringify(template + "\n"))
    config.create_logpath(statements=[generator_source, checkpoint_parser, file_destination])

    syslog_ng.start(config)
    assert file_destination.read_log().strip() == expected_value
