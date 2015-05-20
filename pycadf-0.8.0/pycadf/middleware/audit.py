# Copyright (c) 2013 OpenStack Foundation
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.

"""
Attach open standard audit information to request.environ

AuditMiddleware filter should be placed after Keystone's auth_token middleware
in the pipeline so that it can utilize the information Keystone provides.

"""
import logging

from pycadf._i18n import _LW
from pycadf.audit import api as cadf_api
from pycadf.middleware import notifier


class AuditMiddleware(notifier.RequestNotifier):

    def __init__(self, app, **conf):
        super(AuditMiddleware, self).__init__(app, **conf)
        LOG = logging.getLogger(conf.get('log_name', __name__))
        LOG.warning(_LW('pyCADF middleware is deprecated as of version 0.7.0,'
                        ' in favour of keystonemiddleware.audit.'
                        'AuditMiddleware'))
        map_file = conf.get('audit_map_file', None)
        self.cadf_audit = cadf_api.OpenStackAuditApi(map_file)

    @notifier.log_and_ignore_error
    def process_request(self, request):
        self.cadf_audit.append_audit_event(request)
        super(AuditMiddleware, self).process_request(request)

    @notifier.log_and_ignore_error
    def process_response(self, request, response,
                         exception=None, traceback=None):
        self.cadf_audit.mod_audit_event(request, response)
        super(AuditMiddleware, self).process_response(request, response,
                                                      exception, traceback)