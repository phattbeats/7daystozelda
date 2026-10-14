FROM soh-web:pha4177
COPY server.js /app/server.js
COPY public/ /app/public/
USER root
RUN rm -f /app/public/index.html.pre-fairypicker /app/public/index.html.gz.pre-fairypicker
USER node
