document.addEventListener('DOMContentLoaded', function() {
    // Mostrar solo el SSID mostrado desde la cabecera personalizada
    fetch(window.location.pathname, {cache: "no-store"}).then(resp => {
    document.getElementById('ssid_mostrada').textContent = resp.headers.get('X-SSID-MOSTRADA') || '';
    document.getElementById('password_mostrada').textContent = resp.headers.get('X-PASSWORD-MOSTRADA') || '';
});
    const contactForm = document.getElementById('contactForm');
    const notificationMessage = document.getElementById('notification-message');

    if (contactForm) {
        contactForm.addEventListener('submit', function(event) {
            event.preventDefault();

            let nombre = document.getElementById('nombre').value;
            const telefono = document.getElementById('telefono').value;

            // Reemplaza espacios por espacio simple para enviar
            const nombreEnviar = nombre.replace(/\s+/g, ' ');

            console.log('Formulario enviado:');
            console.log('Nombre con espacios:', nombreEnviar);
            console.log('Teléfono:', telefono);

            // ENVÍA ambos parámetros
            fetch(`/enviar?comando=${encodeURIComponent(nombreEnviar)}&telefono=${encodeURIComponent(telefono)}`)
                .then(response => response.text())
                .then(data => {
                    console.log('Respuesta del servidor:', data);

                    // Muestra mensaje con nombre original (con espacios)
                    notificationMessage.innerText = `Bienvenido Vecino: ${nombre}`;
                    notificationMessage.style.display = 'block';

                    contactForm.reset();

                    setTimeout(() => {
                        notificationMessage.style.display = 'none';
                        notificationMessage.style.opacity = '0';
                    }, 5000);
                })
                .catch(error => {
                    console.error('Error al enviar comando:', error);
                    notificationMessage.innerText = 'Error al enviar el comando';
                    notificationMessage.style.display = 'block';

                    setTimeout(() => {
                        notificationMessage.style.display = 'none';
                        notificationMessage.style.opacity = '0';
                    }, 5000);
                });
        });
    }
});