"""
This file is part of notmuch.

Notmuch is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

Notmuch is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with notmuch.  If not, see <http://www.gnu.org/licenses/>.

Copyright 2010 Sebastian Spaeth <Sebastian@SSpaeth.de>'
"""

from ctypes import CDLL, c_char_p, c_int
from ctypes.util import find_library

#-----------------------------------------------------------------------------
#package-global instance of the notmuch library
try:
    nmlib = CDLL("libnotmuch.so.1")
except:
    raise ImportError("Could not find shared 'notmuch' library.")

#-----------------------------------------------------------------------------
class Enum(object):
    """Provides ENUMS as "code=Enum(['a','b','c'])" where code.a=0 etc..."""
    def __init__(self, names):
        for number, name in enumerate(names):
            setattr(self, name, number)

#-----------------------------------------------------------------------------
class Status(Enum):
    """Enum with a string representation of a notmuch_status_t value."""
    __name__="foo"
    _status2str = nmlib.notmuch_status_to_string
    _status2str.restype = c_char_p
    _status2str.argtypes = [c_int]

    def __init__(self, statuslist):
        """It is initialized with a list of strings that are available as
        Status().string1 - Status().stringn attributes.
        """
        super(Status, self).__init__(statuslist)

    @classmethod
    def status2str(self, status):
        """Get a string representation of a notmuch_status_t value."""   
        # define strings for custom error messages
        if status == STATUS.NOT_INITIALIZED:
          return "Operation on uninitialized object impossible."
        return str(Status._status2str(status))

STATUS = Status(['SUCCESS',
  'OUT_OF_MEMORY',
  'READ_ONLY_DATABASE',
  'XAPIAN_EXCEPTION',
  'FILE_ERROR',
  'FILE_NOT_EMAIL',
  'DUPLICATE_MESSAGE_ID',
  'NULL_POINTER',
  'TAG_TOO_LONG',
  'UNBALANCED_FREEZE_THAW',
  'NOT_INITIALIZED'])


class NotmuchError(Exception):
    def __init__(self, status=None, message=None):
        """Is initiated with a (notmuch.STATUS[,message=None])"""
        super(NotmuchError, self).__init__(message, status)

    def __str__(self):
        if self.args[0] is not None: return self.args[0]
        else: return STATUS.status2str(self.args[1])

