/*
 * Copyright (c) 2018 Balabit
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "logqueue.h"
#include "logqueue-disk.h"
#include "logqueue-disk-reliable.h"
#include "logqueue-disk-non-reliable.h"
#include "diskq.h"
#include "qdisk.c"
#include "apphook.h"
#include "diskq-config.h"

#include "queue_utils_lib.h"
#include "test_diskq_tools.h"
#include <criterion/criterion.h>

#include <stdlib.h>
#include <sys/stat.h>


static LogQueue *
_get_diskqueue(gchar *filename, DiskQueueOptions *options)
{
  LogQueue *q = log_queue_disk_non_reliable_new(options, NULL);
  log_queue_set_use_backlog(q, FALSE);
  log_queue_disk_load_queue(q, filename);
  return q;
}

static LogQueue *
_create_diskqueue(gchar *filename, DiskQueueOptions *options)
{
  LogQueue *q;
  unlink(filename);

  _construct_options(options, 1100000, 0, FALSE);
  options->qout_size = 40;
  q = _get_diskqueue(filename, options);
  return q;
}

static void
_save_diskqueue(LogQueue *q)
{
  gboolean persistent;
  log_queue_disk_save_queue(q, &persistent);
  log_queue_unref(q);
}

static gint64
_get_file_size(LogQueue *q)
{
  struct stat file_stats;
  const gchar *filename = log_queue_disk_get_filename(q);
  assert_gint(stat(filename, &file_stats), 0, "Stat call failed, errno:%d", errno);
  return (gint64)file_stats.st_size;
}

static gint64
_calculate_expected_file_size(gint64 number_of_msgs)
{
  guint diskq_record_len_size = 4;
  gint64 one_msg_size = get_one_message_serialized_size();
  return ((one_msg_size + diskq_record_len_size) * number_of_msgs) + QDISK_RESERVED_SPACE;
}


static gint64
_calculate_truncate_on_push_size(LogQueue *q)
{
  QDisk *qdisk = ((LogQueueDisk *)q)->qdisk;
  // these are already sent out, but still uses disk space
  gint64 consumed_messages_size = qdisk_get_backlog_head(qdisk) - QDISK_RESERVED_SPACE;
  // "active", waiting to be sent out messages
  gint64 queued_messages_size = _calculate_expected_file_size(log_queue_get_length(q));
  return queued_messages_size + consumed_messages_size;
}

static void
_assert_diskq_actual_file_size_with_stored(LogQueue *q)
{
  QDisk *qdisk = ((LogQueueDisk *)q)->qdisk;

  gint64 actual_file_size = _get_file_size(q);
  cr_assert_eq(qdisk->file_size, actual_file_size,
               "File size does not match with stored size; Actual file size: %ld, Expected file size: %ld\n", actual_file_size,
               qdisk->file_size);
}

static void
_assert_diskq_actual_file_size_with_expected(LogQueue *q, gboolean should_file_be_empty,
                                             gint64 number_of_messages_on_disk)
{
  if (should_file_be_empty)
    {
      cr_assert_eq(_get_file_size(q), QDISK_RESERVED_SPACE, "Truncate after load failed: file is not empty");
    }
  else
    {
      // diskq file is truncated on read when it becomes empty,
      // thus we have to assert to the original file size (after push finished), i.e. the max recorded size
      cr_assert_eq(_get_file_size(q), _calculate_expected_file_size(number_of_messages_on_disk),
                   "Truncate after load failed: expected size differs");
    }
}

typedef struct
{
  const gchar *test_id;
  gint number_of_msgs_to_push;
  gint number_of_msgs_to_pop;
  gboolean should_file_be_empty_after_truncate;
  gchar *filename;
} TruncateTestParams;

static void
_test_diskq_truncate(TruncateTestParams params)
{
  LogQueue *q;
  DiskQueueOptions options;

  cr_assert(cfg_init(configuration), "cfg_init failed!");
  q = _create_diskqueue(params.filename, &options);
  cr_assert_eq(log_queue_get_length(q), 0, "No messages should be in a newly created disk-queue file!");

  feed_some_messages(q, params.number_of_msgs_to_push);
  cr_assert_eq(log_queue_get_length(q), params.number_of_msgs_to_push, "Not all messages have been queued!");
  gint64 messages_on_disk_after_push = qdisk_get_length(((LogQueueDisk *)q)->qdisk);
  _assert_diskq_actual_file_size_with_stored(q);

  send_some_messages(q, params.number_of_msgs_to_pop);
  cr_assert_eq(log_queue_get_length(q), params.number_of_msgs_to_push - params.number_of_msgs_to_pop,
               "Invalid number of messages in disk-queue after messages have been popped!");

  _save_diskqueue(q);

  q = _get_diskqueue(params.filename, &options);
  cr_assert_eq(log_queue_get_length(q), params.number_of_msgs_to_push - params.number_of_msgs_to_pop,
               "Invalid number of messages in disk-queue after opened existing one");
  _assert_diskq_actual_file_size_with_stored(q);
  _assert_diskq_actual_file_size_with_expected(q, params.should_file_be_empty_after_truncate,
                                               messages_on_disk_after_push);


  unlink(params.filename);

  log_queue_unref(q);
  disk_queue_options_destroy(&options);
}

// Diskbuffer is the part of disk-queue that is used only when qout is full
Test(diskq_truncate, test_diskq_truncate_with_diskbuffer_used)
{
  _test_diskq_truncate((TruncateTestParams)
  {
    .test_id = __func__,
    .number_of_msgs_to_push = 100,
    .number_of_msgs_to_pop = 50,
    .should_file_be_empty_after_truncate = FALSE,
    .filename = "test_dq_truncate1.qf"
  });
}

Test(diskq_truncate, test_diskq_truncate_without_diskbuffer_used)
{
  _test_diskq_truncate((TruncateTestParams)
  {
    .test_id = __func__,
    .number_of_msgs_to_push = 100,
    .number_of_msgs_to_pop = 80,
    .should_file_be_empty_after_truncate = TRUE,
    .filename = "test_dq_truncate2.qf"
  });
}

static LogQueue *
_create_reliable_diskqueue(gchar *filename, DiskQueueOptions *options, gboolean use_backlog,
                           gdouble truncate_size_ratio)
{
  LogQueue *q;
  unlink(filename);

  _construct_options(options, 1100000, 0, TRUE);

  if (truncate_size_ratio < 0)
    truncate_size_ratio = disk_queue_config_get_truncate_size_ratio(configuration);
  options->truncate_size_ratio = truncate_size_ratio;

  q = log_queue_disk_reliable_new(options, "persist-name");
  log_queue_set_use_backlog(q, use_backlog);
  log_queue_disk_load_queue(q, filename);
  return q;
}


Test(diskq_truncate, test_diskq_truncate_on_push)
{
  const gint full_disk_message_number = 8194; // measured for disk_buf_size 1100000
  const gint read_is_on_end_message_number  = full_disk_message_number - 200;
  const gint write_wraps_message_number = read_is_on_end_message_number - 200;
  const gint read_wraps_message_number = 300;
  const gint trigger_truncate_message_number = 1;
  LogQueue *q;
  GString *filename = g_string_new("test_dq_truncate_on_write.rqf");

  DiskQueueOptions options;
  q = _create_reliable_diskqueue(filename->str, &options, FALSE, -1);
  cr_assert_eq(log_queue_get_length(q), 0, "No messages should be in a newly created disk-queue file!");

  // 1. fill it until its FULL
  feed_some_messages(q, full_disk_message_number);
  cr_assert_eq(log_queue_get_length(q), full_disk_message_number,
               "Not all messages have been queued!");
  gint64 file_size_full = _get_file_size(q);

  // 2. move read pointer just before write pointer
  send_some_messages(q, read_is_on_end_message_number);
  cr_assert_eq(_get_file_size(q), file_size_full,
               "Unexpected disk-queue file truncate happened during pop!");

  // 3. wrap around write pointer
  feed_some_messages(q, write_wraps_message_number);
  // file size can even grow, if not the whole disk_buf_size is filled (i.e. disk_buf_size is not an integer multiple of one message size)
  cr_assert(_get_file_size(q) >= file_size_full,
            "Unexpected disk-queue truncate during push! size:%ld expected:%ld", _get_file_size(q), file_size_full);

  // 4. wrap around read pointer. Truncate only happens if read < write < file_size
  send_some_messages(q, read_wraps_message_number);
  cr_assert(_get_file_size(q) >= file_size_full,
            "Unexpected disk-queue truncate while read wrapped! size:%ld expected:%ld", _get_file_size(q), file_size_full);

  // 5. push some messages to trigger truncate after push
  feed_some_messages(q, trigger_truncate_message_number);
  cr_assert_eq(log_queue_get_length(q),
               (full_disk_message_number - read_is_on_end_message_number
                + write_wraps_message_number - read_wraps_message_number + trigger_truncate_message_number),
               "Invalid queue length, expected number messages mismatch!");
  _assert_diskq_actual_file_size_with_stored(q);
  cr_assert_eq(_get_file_size(q), _calculate_truncate_on_push_size(q), "Truncated size does not match expected");


  unlink(filename->str);
  g_string_free(filename, TRUE);

  _save_diskqueue(q);
}

static void
_assert_cursors_are_at_start(LogQueue *q)
{
  QDisk *qdisk = ((LogQueueDisk *)q)->qdisk;

  cr_assert_eq(qdisk->hdr->read_head, QDISK_RESERVED_SPACE, "Read head was not reset!");
  cr_assert_eq(qdisk->hdr->write_head, QDISK_RESERVED_SPACE, "Write head was not reset!");
  cr_assert_eq(qdisk->hdr->backlog_head, QDISK_RESERVED_SPACE, "Backlog head was not reset!");
}

Test(diskq_truncate, test_diskq_truncate_size_ratio_default)
{
  const gint truncate_threshold = (gint)(1100000 * disk_queue_config_get_truncate_size_ratio(configuration));
  const gint empty_log_msg_size = 134;
  const gint below_threshold_message_number = truncate_threshold / empty_log_msg_size;
  const gint above_threshold_message_number = below_threshold_message_number + 1;

  LogQueue *q;
  GString *filename = g_string_new("test_dq_truncate_size_ratio_default.rqf");

  DiskQueueOptions options;
  q = _create_reliable_diskqueue(filename->str, &options, TRUE, -1);
  cr_assert_eq(log_queue_get_length(q), 0, "No messages should be in a newly created disk-queue file!");

  // 1. fill it just below the truncate threshold
  feed_some_messages(q, below_threshold_message_number);
  cr_assert_eq(log_queue_get_length(q), below_threshold_message_number,
               "Not all messages have been queued!");
  const gint64 below_threshold_file_size = _get_file_size(q);

  // 2. process all messages, no truncate should happen yet, but all 3 heads should be moved to QDISK_RESERVED_SPACE
  send_some_messages(q, below_threshold_message_number);
  log_queue_ack_backlog(q, below_threshold_message_number);
  cr_assert_eq(_get_file_size(q), below_threshold_file_size,
               "Unexpected disk-queue file truncate happened when we were below the truncate threshold!");
  _assert_cursors_are_at_start(q);

  // 3. fill it just above the truncate threshold
  feed_some_messages(q, above_threshold_message_number);
  cr_assert_eq(log_queue_get_length(q), above_threshold_message_number,
               "Not all messages have been queued!");

  // 4. process all messages, we should truncate
  send_some_messages(q, above_threshold_message_number);
  log_queue_ack_backlog(q, above_threshold_message_number);
  cr_assert_eq(_get_file_size(q), QDISK_RESERVED_SPACE,
               "Disk-queue file truncate should have happened when we were above the truncate threshold!");

  _assert_diskq_actual_file_size_with_stored(q);

  unlink(filename->str);
  g_string_free(filename, TRUE);

  _save_diskqueue(q);
}

Test(diskq_truncate, test_diskq_truncate_size_ratio_0)
{
  LogQueue *q;
  GString *filename = g_string_new("test_dq_truncate_size_ratio_0.rqf");

  DiskQueueOptions options;
  q = _create_reliable_diskqueue(filename->str, &options, TRUE, 0);
  cr_assert_eq(log_queue_get_length(q), 0, "No messages should be in a newly created disk-queue file!");

  // 1. feed 1 message
  feed_some_messages(q, 1);
  cr_assert_eq(log_queue_get_length(q), 1,
               "Not all messages have been queued!");

  // 2. process the message, we should truncate even for that 1, and all 3 heads should be moved to QDISK_RESERVED_SPACE
  send_some_messages(q, 1);
  log_queue_ack_backlog(q, 1);
  cr_assert_eq(_get_file_size(q), QDISK_RESERVED_SPACE,
               "Disk-queue file truncate should have happened, because truncate-size-ratio(0) was set!");
  _assert_cursors_are_at_start(q);

  _assert_diskq_actual_file_size_with_stored(q);

  unlink(filename->str);
  g_string_free(filename, TRUE);

  _save_diskqueue(q);
}

Test(diskq_truncate, test_diskq_truncate_size_ratio_1)
{
  const gint full_disk_message_number = 8194; // measured for disk_buf_size 1100000
  LogQueue *q;
  GString *filename = g_string_new("test_dq_truncate_size_ratio_1.rqf");

  DiskQueueOptions options;
  q = _create_reliable_diskqueue(filename->str, &options, TRUE, 1);
  cr_assert_eq(log_queue_get_length(q), 0, "No messages should be in a newly created disk-queue file!");

  // 1. feed to full
  feed_some_messages(q, full_disk_message_number);
  cr_assert_eq(log_queue_get_length(q), full_disk_message_number,
               "Not all messages have been queued!");
  const gint64 full_file_size = _get_file_size(q);

  // 2. process the messages, we should not truncate, but all 3 heads should be moved to QDISK_RESERVED_SPACE
  send_some_messages(q, full_disk_message_number);
  log_queue_ack_backlog(q, full_disk_message_number);
  cr_assert_eq(_get_file_size(q), full_file_size,
               "Unexpected disk-queue file truncate happened when truncate-size-ratio(1) was set!");
  _assert_cursors_are_at_start(q);

  _assert_diskq_actual_file_size_with_stored(q);

  unlink(filename->str);
  g_string_free(filename, TRUE);

  _save_diskqueue(q);
}

static void
setup(void)
{
  msg_init(TRUE); // internal messages will go to stderr(), msg_init() is idempotent
  app_startup();
  configuration = cfg_new_snippet();
}

static void
teardown(void)
{
  app_shutdown();
  cfg_free(configuration);
}

TestSuite(diskq_truncate, .init = setup, .fini = teardown);
