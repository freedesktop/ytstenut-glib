testexecdir = $(libdir)/ytstenut/tests

tests = \
  message \
  $(NULL)

integration_tests = \
  message-send \
  status-set \
  $(NULL)

AM_CPPFLAGS = \
  -I$(top_srcdir) \
	-DG_DISABLE_DEPRECATED \
	-DG_LOG_DOMAIN=\"YtstenutGlib\" \
	$(NULL)

AM_CFLAGS = $(YTS_CFLAGS)

AM_LDFLAGS = ../ytstenut/libytstenut-@YTS_API_VERSION@.la

testexec_PROGRAMS = $(tests) $(integration_tests)

TESTS = $(tests)

if ENABLE_INTEGRATION_TESTS
TESTS += $(integration_tests)
endif

message_SOURCES          = message.c
message_LDADD            = $(YTS_LIBS)

## File transfer can't be tested this way, because it is not possible to do
## FT to self (i.e., there would need to be two separate contacts, but as we
## only have one contact per device, that would mean two machines ...
#file_transfer_SOURCES    = file-transfer.c
#file_transfer_LDADD      = $(YTS_LIBS)

message_send_SOURCES     = message-send.c
message_send_LDADD       = $(YTS_LIBS)

status_set_SOURCES     = status-set.c
status_set_LDADD       = $(YTS_LIBS)

-include $(top_srcdir)/git.mk
